#include "application/session.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace hd;
namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void advance(Session& s,Time& now,Time duration) { const Time end=now+duration; for(;now<end;++now) s.tick(now); s.tick(now); }
Sample sampleFor(std::uint32_t session,std::uint32_t request,double rpm,Time now) {
    Sample s{}; s.session=session;s.request=request;s.time=now;
    s.values={rpm,42.5,80.,-12.3,25.,100.,14.2};s.qualities.fill(Quality::Valid);return s;
}
void pipelineAndManual() {
    Session s;Time now=0;
    s.emulator().setScenario(Scenario::Manual);
    s.emulator().setManual(Channel::Rpm,800);
    std::uint64_t tx=0,rx=0,accepted=0;
    s.onRaw=[&](const RawEvent& e){if(e.kind=="TX")++tx;if(e.kind=="RX")++rx;};
    s.onSample=[&](const Sample& sample){++accepted;check(sample.session==s.id(),"sample session");};
    s.start(now);check(!s.model().current(Channel::Rpm,now),"no slider bypass before response");
    advance(s,now,200);
    check(s.state()==SessionState::Running,"HELLO through stream");
    check(s.model().current(Channel::Rpm,now)==800.,"full request byte emulator byte parser sample path");
    s.emulator().setManual(Channel::Rpm,3500);
    check(s.model().current(Channel::Rpm,now)==800.,"manual write cannot touch model");
    advance(s,now,150);
    check(s.model().current(Channel::Rpm,now)==3500.,"manual value appears via next snapshot");
    check(tx>=3&&rx>tx&&accepted>=2,"normal transport genuinely fragments replies");
    s.emulator().setManual(Channel::Rpm,0);
    advance(s,now,150);
    check(s.model().current(Channel::Rpm,now)==0.,"zero RPM valid");
    s.emulator().setChannelQuality(Channel::Coolant,Quality::Unsupported);
    s.emulator().setChannelQuality(Channel::Intake,Quality::Invalid);
    advance(s,now,150);
    check(s.model().channels()[2].quality==Quality::Unsupported&&!s.model().current(Channel::Coolant,now),"per-channel Unsupported");
    check(s.model().channels()[3].quality==Quality::Invalid&&!s.model().current(Channel::Intake,now),"per-channel Invalid");
    check(s.model().channels()[0].quality==Quality::Valid,"independent channel quality");
}
void staleAndRecovery() {
    Session s; Time now=0;s.start(now);advance(s,now,300);
    const auto last=s.model().channels()[0].lastValid;
    check(last.has_value(),"initial fresh value");
    s.emulator().faults().silent=true;advance(s,now,1500);
    check(s.model().channels()[0].quality==Quality::Stale,"Valid to Stale");
    check(s.model().current(Channel::Rpm,now).has_value(),"stale briefly shown");
    check(s.model().channels()[0].lastValid==last,"silence doesn't change timestamp");
    advance(s,now,2000);
    check(!s.model().current(Channel::Rpm,now),"old value hidden after threshold");
    check(s.stats().timeouts>0&&s.stats().responseHz==0,"timeout and actual response frequency");
    s.emulator().faults().silent=false;advance(s,now,500);
    check(s.model().channels()[0].quality==Quality::Valid&&s.model().current(Channel::Rpm,now),"recovered valid byte response");
}
void damagedPackets() {
    Session s;Time now=0;s.start(now);advance(s,now,80);
    const auto last=s.model().channels()[0].lastValid;
    s.emulator().faults().corruptNext=true;
    advance(s,now,130);
    check(s.stats().corrupt>0,"CRC failure counted");
    check(s.model().channels()[0].lastValid==last,"bad checksum cannot refresh model");
    advance(s,now,400);check(s.model().channels()[0].lastValid>last,"CRC recovery");
    const auto before=s.stats().timeouts;
    s.emulator().faults().truncateNext=true;advance(s,now,650);
    check(s.stats().timeouts>before,"truncated response times out");
    check(s.model().channels()[0].quality==Quality::Valid,"resynchronized after truncation");
}
void matchingAndLifecycle() {
    Session s;Time now=0;std::uint32_t active=0;std::vector<std::uint32_t> timedOut;
    s.onRaw=[&](const RawEvent& e){if(e.kind=="TX")active=e.request;if(e.kind=="timeout")timedOut.push_back(e.request);};
    s.start(now);advance(s,now,100);
    const auto accepted=s.stats().accepted;
    s.receive(encode(Frame{SnapshotResponse,s.id(),2,encodeSnapshot(sampleFor(s.id(),2,9999,now))}),now);
    check(s.stats().accepted==accepted,"duplicate ignored");
    s.emulator().faults().silent=true;advance(s,now,10);
    const auto request=active;const auto oldSession=s.id();
    advance(s,now,400);check(!timedOut.empty(),"controlled timeout");
    const auto before=s.stats().accepted;
    s.receive(encode(Frame{SnapshotResponse,s.id(),request,encodeSnapshot(sampleFor(s.id(),request,9999,now))}),now);
    check(s.stats().accepted==before,"late response ignored");
    s.emulator().faults().silent=false;s.start(now);advance(s,now,50);
    s.receive(encode(Frame{SnapshotResponse,oldSession,active,encodeSnapshot(sampleFor(oldSession,active,9999,now))}),now);
    check(s.model().current(Channel::Rpm,now)!=9999.,"previous session cannot update");
    for(int i=0;i<200;++i) {
        s.emulator().faults().delayMs=1000;
        s.start(now);advance(s,now,20);s.stop(now);
        check(s.pendingDeliveries()==0,"stop cancels pending delivery callbacks");
        check(s.state()==SessionState::Stopped&&!s.model().current(Channel::Rpm,now),"stop clears live model");
    }
    s.emulator().faults().delayMs=0;s.start(now);advance(s,now,100);
    check(s.state()==SessionState::Running&&s.stats().accepted>0,"repeat start recovery");
}
void limitsAndHandshake() {
    Session s;Time now=0;s.emulator().faults().silent=true;s.start(now);advance(s,now,1000);
    check(s.state()==SessionState::Faulted&&s.stats().timeouts==3,"bounded handshake retry");
    check(s.pendingDeliveries()==0,"failed handshake no callbacks");
    s.emulator().faults().silent=false;s.start(now);advance(s,now,100);
    s.emulator().faults().delayMs=1000000;
    advance(s,now,100000);
    check(s.pendingDeliveries()<=256,"long virtual load bounded delivery queue");
    check(s.stats().accepted<=2,"late load cannot invent sample");
    s.stop(now);check(s.pendingDeliveries()==0,"stop clears long-delay queue");
    s.emulator().faults().delayMs=0;s.start(now);advance(s,now,600000);
    check(s.stats().accepted>5900&&s.pendingDeliveries()<32,"ten simulated minutes stable bounded 10Hz polling");
}
}
int main() {
    try { pipelineAndManual(); staleAndRecovery(); damagedPackets(); matchingAndLifecycle(); limitsAndHandshake();
        std::cout<<"session: 5 scenario groups passed (controlled clock, full byte path)\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"session test failed: "<<e.what()<<'\n';return 1;}
}
