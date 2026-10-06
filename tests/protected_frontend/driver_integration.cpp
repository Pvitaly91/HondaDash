#include "one_wire.hpp"
#include "../../firmware/shared/reference_fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Standard C++20 electrical-tool harness. The decoder, echo checking, collision,
// resynchronisation and TX completion below are the actual AVR production Driver.
// Only the external frontend and deterministic interrupt scheduler are models.
namespace {
using hd_onewire::Driver;
using hd_onewire::Received;
#ifdef HD_LEGACY_DRIVER
bool startAnchorNeeded(const Driver &) { return false; }
#else
bool startAnchorNeeded(const Driver &driver) { return driver.startNeedsAnchor(); }
#endif
constexpr double infinity = std::numeric_limits<double>::infinity();
struct Corner {
    std::string name;
    double txFall = .1, txRise = .1, rxFall = 1., rxRise = 1.;
};
struct Transition { double at; bool high; bool valid = true; };
struct Frame { double start, period; uint8_t value; };
struct Result {
    std::string name, path, expectation, reason;
    bool pass = false;
    unsigned tx = 0, rx = 0, collisions = 0, timing = 0, framing = 0;
    double minimumStop = 0, completeAt = 0, lastRelease = 0;
    std::string criterion = "logical_predicate", relation = ">=";
    double measured = 0, limit = 1, margin = -1;
    double sampleMargin = 0;
};
std::vector<Result> results;
bool baselineAnchor = false;
double feasibleDataTail = 9.375, feasibleStopTail = 8.1875, feasibleStampOffset = .125;
void require(bool condition, const std::string &why) {
    if (!condition) throw std::runtime_error(why);
}
std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> out; std::string item; std::istringstream in(line);
    while (std::getline(in,item,',')) out.push_back(item);
    return out;
}
double number(const std::string &s) {
    size_t consumed = 0; const double n = std::stod(s,&consumed);
    require(consumed == s.size() && std::isfinite(n), "invalid finite numeric CSV field"); return n;
}
std::vector<std::vector<std::string>> csv(const std::filesystem::path &path) {
    std::ifstream in(path); require(bool(in), "missing CSV: " + path.string());
    std::string line; std::vector<std::vector<std::string>> rows;
    while (std::getline(in,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (!line.empty()) rows.push_back(split(line));
    }
    require(rows.size()>1,"empty CSV: " + path.string()); return rows;
}
class Model {
  public:
    Driver driver;
    std::vector<Received> received;
    std::vector<Transition> d3Edges, dataEdges, d8Edges;
    std::vector<Transition> samples;
    double now = 0, tickUs = .5, compareLateUs = 0, captureLateUs = 0, outputTailUs = 0;
    double edgeJitterUs = 0;
    std::vector<double> compareLateness;
    std::vector<double> outputTails;
    double dataTailUs = -1, stopTailUs = -1, startTailUs = -1, stampOffsetUs = 0;
    double timerMinUs = 1961.698850, timerResetUs = 1, readyDelayUs = 21285.843;
    bool armed = true, powerGood = true, valid = true, locked = false;
    bool ready = true, buttonHeld = false;
    bool d3 = false, sink = false, peer = false, data = true, d8 = true;
    bool forceLow = false;
    bool invalidAsHigh = false;
    Corner corner;
    double completedAt = 0, lastStopRelease = 0;
    double lastDataRise = -infinity, minStop = infinity;
    unsigned invalidSamples = 0;

    explicit Model(Corner c) : corner(std::move(c)) {}
    uint32_t ticks(double time) const { return uint32_t(std::floor(time/tickUs+1e-9)); }
    double micros(uint32_t value) const { return double(value)*tickUs; }
    void at(double time, std::function<void()> work, unsigned priority=3) {
        require(time >= now-1e-8 && std::isfinite(time), "scheduled event in past");
        events.push(Event{time,priority,serial++,std::move(work)});
    }
    void peerAt(double time, bool low) {
        at(time,[this,low]{ peer=low; reconcile(); });
    }
    void inputAt(double time, bool high, bool isValid=true) {
        at(time,[this,high,isValid]{ updateD8(high,isValid); },0);
    }
    void powerAt(double time, bool good) {
        at(time,[this,good]{
            powerGood=good;
            if (!good) { armed=false; locked=true; ready=false; ++readyGeneration; ++timeoutGeneration; sink=false; reconcile(); }
            else qualifyReady();
            // Rising power never clears latch. Analog validity is independently
            // controlled; a PG event does not imply a valid D8 voltage.
        },0);
    }
    bool rearm() {
        // This call represents a fresh qualified manual button edge after
        // debounce. A held button is deliberately not such an edge.
        if (!powerGood || d3 || !ready || buttonHeld || now-lastD3Low<timerResetUs) return false;
        armed=true; locked=false; buttonHeld=true; return true;
    }
    void qualifyReady() {
        if (ready || !powerGood || d3 || buttonHeld) return;
        const uint64_t generation=++readyGeneration;
        at(now+readyDelayUs,[this,generation]{
            if (generation==readyGeneration && powerGood && !d3 && !buttonHeld) ready=true;
        },0);
    }
    void buttonAt(double time,bool held) {
        at(time,[this,held]{
            buttonHeld=held;
            if (held) ++readyGeneration;
            else qualifyReady();
        },0);
    }
    void enqueue(const std::vector<uint8_t> &bytes) {
        require(bytes.size() <= Driver::Capacity, "queue capacity is production 16 bytes");
        for (uint8_t b:bytes) require(driver.enqueue(b),"production enqueue failed");
        driver.service(ticks(now), valid ? d8 : false);
        applyOutput();
    }
    void run(double end) {
        require(end>=now,"run reversed time");
        for (unsigned count=0; count<2000000; ++count) {
            const double late=compareIndex<compareLateness.size()?compareLateness[compareIndex]:compareLateUs;
            double compare = driver.hasEvent() ? micros(driver.nextEvent())+late : infinity;
            if (compare<now) compare=now;
            const double external=events.empty()?infinity:events.top().time;
            if (std::min(compare,external)>end) { now=end; drain(); return; }
            // Capture beats COMPA at an equal timestamp, as on ATmega328P.
            if (external<=compare) {
                Event e=events.top(); events.pop(); now=e.time; e.work();
            } else {
                now=compare;
                ++compareIndex;
                if (!valid) ++invalidSamples;
                const bool sampled=valid ? d8 : invalidAsHigh;
                samples.push_back({now,sampled,valid});
                driver.timer(ticks(now),sampled);
                applyOutput();
            }
            drain();
        }
        throw std::runtime_error("event budget exceeded; never an expected rejection");
    }
  private:
    struct Event {
        double time; unsigned priority; uint64_t serial; std::function<void()> work;
        bool operator<(const Event &b) const {
            if (time!=b.time) return time>b.time;
            return priority!=b.priority ? priority>b.priority : serial>b.serial;
        }
    };
    std::priority_queue<Event> events;
    uint64_t serial=0, outputGeneration=0, dataGeneration=0, timeoutGeneration=0, readyGeneration=0;
    double lastD3Low=-infinity;
    uint16_t previousCompleted=0;
    size_t compareIndex=0;
    size_t outputIndex=0;
    void drain() {
        Received sample{};
        while (driver.pop(sample)) received.push_back(sample);
        if (driver.completedTxSequence()!=previousCompleted) {
            previousCompleted=driver.completedTxSequence(); completedAt=now;
        }
    }
    void applyOutput() {
        const bool requested=driver.drivingLow(), anchor=driver.stopNeedsAnchor(), startAnchor=startAnchorNeeded(driver);
        if (requested==d3 && !anchor && !startAnchor) return;
        const uint64_t generation=++outputGeneration;
        double tail=outputIndex<outputTails.size()?outputTails[outputIndex]:outputTailUs;
        if (startAnchor && startTailUs>=0) tail=startTailUs;
        else if (anchor && stopTailUs>=0) tail=stopTailUs;
        else if (dataTailUs>=0) tail=dataTailUs;
        ++outputIndex;
        at(now+tail,[this,requested,anchor,startAnchor,generation]{
            if (generation!=outputGeneration) return;
            if (requested!=driver.drivingLow()) return;
            if (requested!=d3) {
                d3=requested; d3Edges.push_back({now,d3}); // D3 HIGH requests sink.
                if (d3) {
                    ++readyGeneration;
                    const uint64_t expiry=++timeoutGeneration;
                    if (armed && powerGood && !locked) at(now+timerMinUs,[this,expiry]{
                        if (expiry!=timeoutGeneration || !d3) return;
                        locked=true; armed=false; ready=false; ++readyGeneration; sink=false; reconcile();
                    },0);
                } else {
                    lastD3Low=now; ++timeoutGeneration;
                    qualifyReady();
                }
                const bool target=d3 && armed && !locked && powerGood;
                at(now+(target?corner.txFall:corner.txRise),[this,target,generation]{
                    if (generation!=outputGeneration) return;
                    sink=target && d3 && armed && !locked && powerGood; reconcile();
                },1);
            }
            if (anchor && driver.stopNeedsAnchor()) {
                lastStopRelease=now; driver.outputApplied(ticks(now+stampOffsetUs));
            }
            if (startAnchor && startAnchorNeeded(driver)) driver.outputApplied(ticks(now+stampOffsetUs));
        },1);
    }
    void reconcile() {
        const bool newData=!sink && !peer && !forceLow;
        if (newData==data) return;
        data=newData; dataEdges.push_back({now,data});
        if (data) lastDataRise=now;
        else if (std::isfinite(lastDataRise)) minStop=std::min(minStop,now-lastDataRise);
        const uint64_t generation=++dataGeneration;
        at(now+(data?corner.rxRise:corner.rxFall),[this,generation,newData]{
            if (generation==dataGeneration) updateD8(newData,true);
        },0);
    }
    void updateD8(bool high,bool isValid) {
        const bool falling=d8 && !high;
        d8=high; valid=isValid; d8Edges.push_back({now,high,isValid});
        if (falling && driver.captureNeeded()) {
            // Production ICNC1 uses four CPU cycles, half a Timer1 /8 tick.
            const double filter=tickUs*.5;
            const uint32_t captured=ticks(now+filter);
            at(now+filter+captureLateUs,[this,captured]{ driver.fallingEdge(captured); applyOutput(); },0);
        }
    }
};
void waveform(Model &m, const std::vector<uint8_t> &bytes, double start, double period,
              bool badStop=false, bool inputOnly=false) {
    bool previous=true;
    for (uint8_t value:bytes) {
        for (unsigned bit=0; bit<10; ++bit) {
            const bool high=bit==0?false:bit==9?!badStop:bool(value&(1u<<(bit-1)));
            if (high!=previous) {
                const double time=start+bit*period+(bit==0?0:(bit%2?m.edgeJitterUs:-m.edgeJitterUs));
                if (inputOnly) m.inputAt(time+(high?m.corner.rxRise:m.corner.rxFall),high);
                else m.peerAt(time,!high);
            }
            previous=high;
        }
        start+=10*period;
    }
    if (!previous) {
        if (inputOnly) m.inputAt(start+m.corner.rxRise,true);
        else m.peerAt(start,false);
    }
}
Result report(const std::string &name,const std::string &path,const std::string &expectation,
              const Model &m,bool pass,const std::string &reason) {
    const auto &c=m.driver.counters();
    Result r{name,path,expectation,reason,pass,c.txBytes,unsigned(m.received.size()),
            c.collisions,c.timing,c.framing,std::isfinite(m.minStop)?m.minStop:0,
            m.completedAt,m.lastStopRelease};
    r.measured=pass?1:0; r.margin=r.measured-r.limit;
    return r;
}
void criterion(Result &r,const std::string &name,double value,double limit,const std::string &relation=">=") {
    r.criterion=name; r.measured=value; r.limit=limit; r.relation=relation;
    r.margin=relation==">="?value-limit:limit-value;
}
void rxAll(const Corner &corner, double period, double late, double capture, double jitter) {
    unsigned good=0;
    for (unsigned b=0;b<256;++b) {
        Model m(corner); m.compareLateUs=late; m.captureLateUs=capture; m.edgeJitterUs=jitter;
        waveform(m,{uint8_t(b)},100,period); m.run(1500);
        good+=m.received.size()==1 && m.received[0].byte==b && !m.driver.faulted();
    }
    Model count(corner);
    auto result=report("rx_all256_"+corner.name+"_p"+std::to_string(period)+"_late"+
        std::to_string(late)+"_capture"+std::to_string(capture)+"_jitter"+std::to_string(jitter),"closed_loop","ACCEPT",count,good==256,
        "production RX decoded "+std::to_string(good)+"/256; capture timestamp retained");
    result.rx=good; criterion(result,"correctly_decoded_bytes",good,256); results.push_back(result);
}
void txAll(const Corner &corner, double late,double tail,double tick) {
    unsigned good=0;
    for (unsigned b=0;b<256;++b) {
        Model m(corner); m.tickUs=tick; m.compareLateUs=late*tick/.5; m.outputTailUs=tail;
        m.enqueue({uint8_t(b)}); m.run(1800);
        good+=m.driver.txComplete() && m.driver.counters().echoBytes==1 && !m.driver.faulted()
            && m.received.empty() && !m.sink && !m.locked;
    }
    Model count(corner);
    auto result=report("tx_all256_"+corner.name+"_late"+std::to_string(late)+"_tail"+
        std::to_string(tail)+"_tick"+std::to_string(tick),"closed_loop","ACCEPT",count,good==256,
        "production TX+echo passed "+std::to_string(good)+"/256; own echo absent from RX");
    result.tx=good; criterion(result,"echo_verified_tx_bytes",good,256); results.push_back(result);
}
void packets(const Corner &corner) {
    std::vector<std::vector<uint8_t>> tx{{0,0,0,0,0,0,0,0},
        {std::begin(hd_bridge::WakeBytes),std::end(hd_bridge::WakeBytes)},
        {0x20,5,0,2,0xd9},{0x20,5,0x10,1,0xca},{0x20,5,0x14,1,0xc6}};
    for (unsigned p=0;p<tx.size();++p) {
        Model m(corner); m.enqueue(tx[p]); m.run(15000);
        const bool okay=m.driver.counters().txBytes==tx[p].size() && !m.driver.faulted() && !m.locked;
        results.push_back(report("tx_packet_"+std::to_string(p)+"_"+corner.name,"closed_loop","ACCEPT",m,okay,
            "current wake/read bytes and back-to-back00 use production queue and echo"));
    }
    for (unsigned scenario:{0u,1u,3u}) for (unsigned address:{0u,0x10u,0x14u}) {
        uint8_t bytes[8]{}; const unsigned n=hd_bridge::rawFixture(uint8_t(scenario),uint8_t(address),address==0?2:1,bytes);
        {
            Model sender(corner); sender.enqueue(std::vector<uint8_t>(bytes,bytes+n)); sender.run(8000);
            const bool sent=sender.driver.counters().txBytes==n && !sender.driver.faulted() && !sender.locked;
            results.push_back(report("tx_fixture_"+std::to_string(scenario)+"_"+std::to_string(address)+"_"+corner.name,
                "closed_loop","ACCEPT",sender,sent,"same shared production driver sends every A/B/Boundary raw response and echo"));
        }
        std::vector<uint8_t> response(bytes,bytes+n); response.push_back(0x7e); // intentionally trailing raw byte
        Model m(corner); m.compareLateUs=20; m.captureLateUs=25;
        waveform(m,response,100,102); m.run(8000);
        bool okay=m.received.size()==response.size() && !m.driver.faulted();
        for (unsigned i=0;i<m.received.size() && i<response.size();++i) okay=okay && m.received[i].byte==response[i];
        results.push_back(report("rx_fixture_"+std::to_string(scenario)+"_"+std::to_string(address)+"_"+corner.name,
            "closed_loop","ACCEPT",m,okay,"A/B/Boundary plus trailing byte; driver does not assign protocol ownership"));
    }
}
void faults(const Corner &corner) {
    {
        Model m(corner); m.inputAt(100,false); m.inputAt(110,true); m.run(500);
        const bool okay=m.driver.counters().falseStarts==1 && m.received.empty() && !m.driver.drivingLow();
        results.push_back(report("false_start_"+corner.name,"closed_loop","DETECT",m,okay,"FalseStart; no decoded sample"));
    }
    {
        Model m(corner); waveform(m,{0},100,104,true); m.run(1600);
        const bool okay=m.driver.counters().framing==1 && m.received.size()==1 && m.driver.faulted();
        results.push_back(report("framing_"+corner.name,"closed_loop","DETECT",m,okay,"bad raw stop retained with sticky Framing; not valid protocol result"));
    }
    {
        Model m(corner); m.timerMinUs=300; m.enqueue({0}); m.run(2000);
        const bool okay=m.locked && !m.sink && m.driver.counters().collisions==1 && !m.driver.drivingLow();
        results.push_back(report("short_cutoff_"+corner.name,"closed_loop","REJECT",m,okay,"300us cutoff corrupts00; production echo Collision releases subsequent outputs"));
    }
    {
        Model m(corner); m.locked=true; m.armed=false; m.ready=false; m.enqueue({0}); m.run(500);
        const bool okay=m.driver.counters().collisions==1 && !m.driver.drivingLow() && !m.sink;
        results.push_back(report("locked_latch_"+corner.name,"closed_loop","DETECT",m,okay,"LOW request missing on DATA; Collision, without invented latch feedback"));
    }
    {
        Model m(corner); m.locked=true; m.armed=false; m.ready=false; m.d3=true;
        const bool denied=!m.rearm() && !m.armed && m.locked;
        m.d3=false; m.qualifyReady(); m.run(25);
        const bool notTooEarly=!m.rearm() && !m.ready && m.locked;
        m.run(21286);
        const bool accepted=m.rearm() && m.armed && !m.locked;
        results.push_back(report("explicit_rearm_"+corner.name,"closed_loop","DETECT",m,denied && notTooEarly && accepted,
            "activeTX and readiness<21.286ms cannotarm; only fresh qualified manual action after stable released-button/TXLOW/PG permits sink"));
    }
    {
        Model m(corner); m.buttonAt(0,true); m.powerAt(10,false); m.powerAt(100,true); m.run(65000);
        const bool heldCannotArm=!m.ready && !m.rearm() && !m.armed && m.locked;
        m.buttonAt(66000,false); m.run(88000);
        const bool releasedReady=m.ready && m.rearm() && m.armed && !m.locked;
        results.push_back(report("held_button_powercycle_"+corner.name,"closed_loop","DETECT",m,heldCannotArm && releasedReady,
            "button held across powercycle cannotarm; release qualifies READY then a separate fresh debouncedpress is required"));
    }
    for (bool invalidHigh:{false,true}) {
        Model m(corner); m.invalidAsHigh=invalidHigh; m.enqueue({0}); m.powerAt(350,false); m.inputAt(355,false,false);
        m.powerAt(650,true); m.inputAt(655,false,false); m.run(1800);
        const bool noValid=m.invalidSamples>0 && m.driver.faulted() && m.locked && !m.sink;
        results.push_back(report("brownout_invalid_"+corner.name+"_sample"+std::to_string(invalidHigh),"closed_loop","DETECT",m,noValid,
            "undefined D8 branch sampled "+std::string(invalidHigh?"HIGH":"LOW")+" during00: error+lockedrelease; no production validity signal exists"));
    }
    {
        Model m(corner); m.compareLateUs=20.5; waveform(m,{0xff},100,104); m.run(1500);
        results.push_back(report("late_compare_"+corner.name,"closed_loop","REJECT",m,m.driver.counters().timing>0,
            "20.5us exceeds unchanged 20us production MaxLateTicks"));
    }
    {
        Corner bad=corner; bad.txFall=60; bad.txRise=60; bad.rxFall=30; bad.rxRise=30;
        Model m(bad); m.enqueue({0}); m.run(1800);
        results.push_back(report("slow_frontend_"+corner.name,"closed_loop","REJECT",m,m.driver.counters().collisions>0,
            "90us echo delay misses52us centre; production Collision, not scenario-name rejection"));
    }
}
void stopAndFastPeer(const Corner &corner) {
    Model pair(corner); pair.enqueue({0,0}); pair.run(2800);
    const bool okay=!pair.driver.faulted() && pair.driver.counters().txBytes==2 && pair.minStop>=104.-1e-7;
    const bool counterexample=corner.txRise>corner.txFall && pair.minStop<104. && !pair.driver.faulted();
    auto result=report("back_to_back_stop_width_"+corner.name,"closed_loop",baselineAnchor && corner.txRise>corner.txFall?"REJECT":"ACCEPT",pair,
        baselineAnchor && corner.txRise>corner.txFall?counterexample:okay,
        "measured modeled DATA HIGH between own00 frames must be at least104us; D3 timestamp alone is insufficient");
    criterion(result,"actual_DATA_stop_high_us",pair.minStop,104); results.push_back(result);

    // The peer waits a full104us from the actual DATA rise at bridge stop.
    Model fast(corner); fast.compareLateUs=20; fast.captureLateUs=25;
    fast.enqueue({0});
    const double stop=9*104+20+corner.txRise;
    waveform(fast,{0xa5},stop+104,102); fast.run(3000);
    const bool accepted=!fast.driver.faulted() && fast.driver.counters().txBytes==1
        && fast.received.size()==1 && fast.received[0].byte==0xa5;
    results.push_back(report("fast_peer_"+corner.name,"closed_loop","ACCEPT",fast,accepted,
        "peer starts after full DATA stop; deferred capture keeps edge timestamp and outranks pending COMPA"));
}
void stopClockBoundary() {
    const Corner release{"release_allocation_limit",0,8,0,0};
    for (double tick:{.49,.5,.51}) {
        Model m(release); m.tickUs=tick; m.enqueue({0,0}); m.run(2800);
        const double minimum=Driver::BitTicks*tick;
        const bool actual=m.minStop>=minimum-1e-7 && !m.driver.faulted();
        auto result=report("stop_clock_boundary_"+std::to_string(tick),"closed_loop",baselineAnchor?"REJECT":"ACCEPT",m,
            baselineAnchor?!actual:actual,"8us physical DATA release at oscillatorcorner; ownHIGH width must exceed sender's full bit period");
        criterion(result,"actual_DATA_stop_high_us",m.minStop,minimum); results.push_back(result);
    }
}
void pairedProduction(const Corner &corner,bool variableOutput=false) {
    unsigned correct=0, errors=0;
    for (unsigned byte=0;byte<256;++byte) {
        Model sender(corner); sender.outputTailUs=16;
        sender.compareLateness.assign(40,0);
        sender.compareLateness[19]=20; // first completion/start of queued second frame
        if (variableOutput) { sender.outputTailUs=0; sender.outputTails={0,0,16}; }
        sender.enqueue({0,uint8_t(byte)}); sender.run(3000);
        Model receiver(corner); receiver.tickUs=.51; receiver.compareLateUs=40*receiver.tickUs; receiver.captureLateUs=25;
        for (const auto &edge:sender.d8Edges) receiver.inputAt(edge.at,edge.high,edge.valid);
        receiver.run(3300);
        const bool okay=!sender.driver.faulted() && receiver.received.size()==2 && receiver.received[0].byte==0
            && receiver.received[1].byte==byte && !receiver.driver.faulted();
        correct+=okay; errors+=receiver.driver.faulted();
    }
    Model aggregate(corner);
    auto result=report("paired_queued_tx_mixed_isr_"+corner.name+(variableOutput?"_variableTail":"_constantTail"),"real_sender_feedback_then_real_receiver_replay",baselineAnchor?"REJECT":"ACCEPT",aggregate,baselineAnchor?correct<256:correct==256,
        "real sender D3/frontend trace into real receiver: queued-start COMPA late20us; following compareson-time; RX clock+2% and40ticklateness; correct="+
        std::to_string(correct)+"/256 stickyerrors="+std::to_string(errors));
    result.tx=512; result.rx=correct*2; criterion(result,"correctly_decoded_second_bytes",correct,256); results.push_back(result);
}
double samplingMargin(const Model &receiver,const std::vector<Transition> &edges,const std::vector<uint8_t> &bytes) {
    if (receiver.samples.size()<bytes.size()*10) return -1;
    double margin=infinity;
    for (size_t i=0;i<bytes.size()*10;++i) {
        const auto &sample=receiver.samples[i]; const unsigned bit=unsigned(i%10);
        const bool expected=bit==0?false:bit==9?true:bool(bytes[i/10]&(1u<<(bit-1)));
        double previous=-infinity, next=infinity;
        for (const auto &edge:edges) if (edge.at<=sample.at) previous=edge.at; else {next=edge.at;break;}
        double distance=std::min(sample.at-previous,next-sample.at);
        require(std::isfinite(distance),"sample has no finite waveform edge");
        if (!sample.valid || sample.high!=expected) distance=-distance;
        margin=std::min(margin,distance);
    }
    return margin;
}
void pairedEnvelope(const Corner &corner,double receiverTick,double rxLate,double capture,unsigned pattern,bool badTail=false) {
    unsigned correct=0, sticky=0; double minimumMargin=infinity;
    for (unsigned byte=0;byte<256;++byte) {
        Model sender(corner); sender.startTailUs=16; sender.dataTailUs=badTail?16:feasibleDataTail;
        sender.stopTailUs=feasibleStopTail; sender.stampOffsetUs=feasibleStampOffset;
        sender.compareLateness.assign(40,0);
        for (unsigned event=0;event<40;++event) {
            if (pattern==0) sender.compareLateness[event]=event==19?20:0;
            else if (pattern==1) sender.compareLateness[event]=(event%2)?20:0;
            else sender.compareLateness[event]=(event%4==1 || event%4==2)?20:0;
        }
        sender.enqueue({0,uint8_t(byte)}); sender.run(3300);
        Model receiver(corner); receiver.tickUs=receiverTick; receiver.compareLateUs=rxLate*receiverTick/.5; receiver.captureLateUs=capture;
        for (const auto &edge:sender.d8Edges) receiver.inputAt(edge.at,edge.high,edge.valid);
        receiver.run(3600);
        const bool okay=!sender.driver.faulted() && receiver.received.size()==2 && receiver.received[0].byte==0
            && receiver.received[1].byte==byte && !receiver.driver.faulted();
        correct+=okay; sticky+=receiver.driver.faulted();
        minimumMargin=std::min(minimumMargin,samplingMargin(receiver,sender.d8Edges,{0,uint8_t(byte)}));
    }
    Model aggregate(corner);
    auto result=report("paired_envelope_"+corner.name+"_tick"+std::to_string(receiverTick)+"_rxLate"+std::to_string(rxLate)+
        "_capture"+std::to_string(capture)+"_pattern"+std::to_string(pattern)+(badTail?"_bad16usDataTail":""),
        "real_sender_feedback_then_real_receiver_replay",badTail?"REJECT":"ACCEPT",aggregate,badTail?correct<256:correct==256,
        "actualproduction TX/RX; independent entryphases and verified data/stop tails; correct="+std::to_string(correct)+"/256 stickyerrors="+std::to_string(sticky));
    result.tx=512; result.rx=correct*2; result.sampleMargin=minimumMargin;
    criterion(result,"correctly_decoded_second_bytes",correct,256); results.push_back(result);
}
void traceReplay(const std::filesystem::path &folder) {
    if (folder.empty()) return;
    const auto manifest=csv(folder/"manifest.csv");
    require(manifest[0]==std::vector<std::string>({"name","transitions","frames","expected"}),"trace manifest header mismatch");
    for (size_t r=1;r<manifest.size();++r) {
        const auto &entry=manifest[r]; require(entry.size()==4,"manifest field count");
        require(entry[3]=="ACCEPT" || entry[3]=="PASS" || entry[3]=="REJECT","manifest expectation must be ACCEPT/PASS or REJECT");
        const std::string expectation=entry[3]=="PASS"?"ACCEPT":entry[3];
        const auto transitions=csv(folder/entry[1]), frames=csv(folder/entry[2]);
        require(transitions[0]==std::vector<std::string>({"time_s","level","valid"}),"transition header mismatch");
        require(frames[0]==std::vector<std::string>({"direction","start_s","period_s","value"}),"frame header mismatch");
        for (double late:{0.,20.}) for (double cap:{0.,25.}) {
            Model m({entry[0]}); m.compareLateUs=late; m.captureLateUs=cap;
            double finish=0, previousTime=-1;
            for (size_t i=1;i<transitions.size();++i) {
                require(transitions[i].size()==3,"transition field count");
                const double time=number(transitions[i][0])*1e6;
                const double level=number(transitions[i][1]), valid=number(transitions[i][2]);
                require(time>=0 && time>=previousTime,"transition timestamps must be monotonic and nonnegative");
                require((level==0 || level==1) && (valid==0 || valid==1),"transition level/valid must be0 or1");
                previousTime=time;
                m.inputAt(time,level==1,valid==1); finish=std::max(finish,time);
            }
            double previousStart=-1;
            for (size_t i=1;i<frames.size();++i) {
                require(frames[i].size()==4,"frame field count");
                require(frames[i][0]=="tx" || frames[i][0]=="rx","frame direction must be tx or rx");
                const double start=number(frames[i][1]), period=number(frames[i][2]), value=number(frames[i][3]);
                require(start>=0 && start>=previousStart && period>0,"frame timing invalid");
                require(value>=0 && value<=255 && value==std::floor(value),"frame byte must be integer0..255");
                previousStart=start;
            }
            m.run(finish+1500);
            bool okay=m.received.size()==frames.size()-1 && !m.driver.faulted();
            for (size_t i=1;i<frames.size();++i) {
                require(frames[i].size()==4,"frame field count");
                if (i<=m.received.size()) okay=okay && m.received[i-1].byte==unsigned(number(frames[i][3]));
            }
            const bool expected=expectation=="ACCEPT"?okay:!okay && m.driver.faulted();
            results.push_back(report("spice_"+entry[0]+"_late"+std::to_string(late)+"_capture"+std::to_string(cap),
                "precomputed_spice_d8",expectation,m,expected,"production RX receives extracted analog D8 transitions; no TX feedback in replay"));
        }
    }
}
std::string json(const std::string &s) {
    std::string out="\""; for (char c:s) { if (c=='\\'||c=='\"') out+='\\'; out+=c; } return out+'\"';
}
void write(const std::filesystem::path &out) {
    std::filesystem::create_directories(out);
    std::ofstream j(out/"driver-integration.json"), c(out/"driver-integration.csv");
    unsigned failed=0; for (const auto &r:results) failed+=!r.pass;
    j << std::setprecision(12) << "{\n  \"status\": " << json(failed?"FAIL":"PASS")
      << ",\n  \"production_source\": \"firmware/shared/one_wire.cpp\",\n  \"physical_status\": \"NOT VERIFIED\","
      << "\n  \"tick_us\": 0.5, \"bit_us\": 104, \"max_compare_lateness_us\": 20,"
      << "\n  \"gpio_allocations\": {\"data_tail_us\":" << feasibleDataTail << ",\"stop_tail_us\":" << feasibleStopTail
      << ",\"gpio_stamp_us\":" << feasibleStampOffset << "},"
      << "\n  \"instruction_level_avr\": \"NOT RUN by this harness\",\n  \"anchor_baseline\": " << (baselineAnchor?"true":"false") << ",\n  \"checks\": [\n";
    c << "scenario,path,expectation,status,tx_bytes,rx_bytes,collisions,timing,framing,minimum_data_high_us,tx_complete_us,d3_stop_release_us,criterion,measured,relation,limit,margin,sample_margin_us,reason\n";
    for (size_t i=0;i<results.size();++i) {
        const auto &r=results[i];
        const std::string status=r.pass?(r.expectation=="REJECT"?"EXPECTED_REJECTION":"PASS"):"FAIL";
        j << "    {\"scenario\":" << json(r.name) << ",\"path\":" << json(r.path) << ",\"expectation\":" << json(r.expectation)
          << ",\"status\":" << json(status) << ",\"tx_bytes\":" << r.tx << ",\"rx_bytes\":" << r.rx
          << ",\"collisions\":" << r.collisions << ",\"timing\":" << r.timing << ",\"framing\":" << r.framing
          << ",\"minimum_data_high_us\":" << r.minimumStop << ",\"tx_complete_us\":" << r.completeAt
          << ",\"d3_stop_release_us\":" << r.lastRelease << ",\"criterion\":" << json(r.criterion)
          << ",\"measured\":" << r.measured << ",\"relation\":" << json(r.relation) << ",\"limit\":" << r.limit
          << ",\"margin\":" << r.margin << ",\"sample_margin_us\":" << r.sampleMargin << ",\"reason\":" << json(r.reason) << "}" << (i+1<results.size()?",":"") << '\n';
        // Criteria remain explicit even for an expected rejection: its physical
        // margin is negative while detection of that bad margin is a test PASS.
        c << r.name << ',' << r.path << ',' << r.expectation << ',' << status << ',' << r.tx << ',' << r.rx << ','
          << r.collisions << ',' << r.timing << ',' << r.framing << ',' << r.minimumStop << ',' << r.completeAt << ',' << r.lastRelease << ','
          << r.criterion << ',' << r.measured << ',' << r.relation << ',' << r.limit << ',' << r.margin << ',' << r.sampleMargin << ',' << r.reason << '\n';
    }
    j << "  ],\n  \"passed\": " << results.size()-failed << ", \"failed\": " << failed << "\n}\n";
    require(bool(j)&&bool(c),"report write failure");
}
}
int main(int argc,char **argv) {
    try {
        std::filesystem::path out="build/driver-integration", traces, calibration;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i]; require(i+1<argc,"argument requires value");
            if (arg=="--out") out=argv[++i];
            else if (arg=="--trace-dir") traces=argv[++i];
            else if (arg=="--calibration") calibration=argv[++i];
            else if (arg=="--anchor-baseline") { require(std::string(argv[++i])=="true","baseline flag expects true"); baselineAnchor=true; }
            else if (arg=="--data-tail-us") feasibleDataTail=number(argv[++i]);
            else if (arg=="--stop-tail-us") feasibleStopTail=number(argv[++i]);
            else if (arg=="--gpio-stamp-us") feasibleStampOffset=number(argv[++i]);
            else throw std::runtime_error("unknown argument "+arg);
        }
        std::vector<Corner> corners{{"min",.08,.08,.3,.3},{"max_asymmetric",.25,1.5,3.,4.5}};
        if (!calibration.empty()) {
            const auto rows=csv(calibration);
            require(rows[0]==std::vector<std::string>({"name","tx_fall_us","tx_rise_us","rx_fall_us","rx_rise_us","echo_fall_us","echo_rise_us"}),"calibration header mismatch");
            corners.clear();
            for (size_t i=1;i<rows.size();++i) {
                require(rows[i].size()==7,"calibration field count");
                corners.push_back({rows[i][0],number(rows[i][1]),number(rows[i][2]),number(rows[i][3]),number(rows[i][4])});
                for (double n:{corners.back().txFall,corners.back().txRise,corners.back().rxFall,corners.back().rxRise}) require(n>=0,"negative frontend delay");
                require(std::abs(corners.back().txFall+corners.back().rxFall-number(rows[i][5]))<.5 &&
                    std::abs(corners.back().txRise+corners.back().rxRise-number(rows[i][6]))<.5,
                    "SPICE calibration separate-path sum differs from observed echo by >=0.5us");
            }
        }
        for (const auto &corner:corners) {
            for (double period:{101.92,104.,106.08}) for (double late:{0.,20.}) for (double cap:{0.,25.}) for (double jitter:{-2.,2.}) rxAll(corner,period,late,cap,jitter);
            for (double late:{0.,20.}) for (double tail:{0.,16.}) for (double tick:{.49,.5,.51}) txAll(corner,late,tail,tick);
            packets(corner); faults(corner); stopAndFastPeer(corner);
            pairedProduction(corner); pairedProduction(corner,true);
            if (!baselineAnchor) {
                for (double tick:{.49,.51}) for (double rxLate:{0.,20.}) for (double capture:{0.,25.}) for (unsigned pattern:{0u,1u,2u})
                    pairedEnvelope(corner,tick,rxLate,capture,pattern);
                pairedEnvelope(corner,.49,0,25,1,true);
            }
        }
        stopClockBoundary();
        traceReplay(traces); write(out);
        unsigned failed=0;
        for (const auto &r:results) if (!r.pass) { ++failed; std::cerr << r.name << ": " << r.reason << '\n'; }
        std::cout << "production driver/frontend " << (failed?"FAIL":"PASS") << " " << results.size()-failed << '/' << results.size()
                  << "; physical NOT VERIFIED\n";
        return failed?1:0;
    } catch (const std::exception &e) { std::cerr << "harness ERROR (never expected rejection): " << e.what() << '\n'; return 3; }
}
