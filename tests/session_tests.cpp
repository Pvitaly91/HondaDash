#include "application/session.hpp"
#include "transport/in_memory_transport.hpp"
#include <iostream>
#include <stdexcept>
using namespace hd;
namespace {
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void advance(Session &s, Time &n, Time d) {
    auto end = n + d;
    for (; n < end; ++n)
        s.tick(n);
    s.tick(n);
}
const std::array<double, 7> manual{3500, 42.5, 80, -12.3, 25, 100, 14.2};
// Retains copied callbacks intentionally: tests Session's own lifecycle guard,
// independently of a well-behaved transport's cancellation implementation.
struct Fake : Transport {
    TransportCallbacks cb;
    bool open{}, drain{true}, reply{};
    std::size_t queued{};
    std::vector<Frame> requests;
    Parser parser;
    void start(TransportCallbacks c, Time n) override {
        cb = std::move(c);
        open = true;
        cb.opened(n);
    }
    void close() override {
        open = false;
        queued = 0;
    }
    bool send(std::span<const std::uint8_t> b, Time n) override {
        auto f = parser.feed(b);
        requests.insert(requests.end(), f.begin(), f.end());
        queued = b.size();
        if (drain) {
            queued = 0;
            cb.sent(n);
        }
        return true;
    }
    void tick(Time) override {}
    bool isOpen() const override { return open; }
    std::size_t pendingBytes() const override { return queued; }
    std::string name() const override { return "test"; }
    void response(std::uint8_t type, std::vector<std::uint8_t> p, Time n) {
        auto f = requests.back();
        cb.received(encode({type, f.session, f.request, std::move(p)}), n);
    }
};
void pipeline() {
    InMemoryTransport t;
    Session s(t);
    Time n = 0;
    s.setScenario(3, 42);
    s.setManual(manual);
    unsigned tx = 0, rx = 0;
    s.onRaw = [&](const RawEvent &e) {
        tx += e.kind == "TX";
        rx += e.kind == "RX";
    };
    s.start(n);
    check(!s.model().current(Channel::Rpm, n), "no slider bypass");
    advance(s, n, 300);
    check(s.state() == SessionState::Running && s.deviceInfo()->endpoint == "desktop-emulator", "HELLO+INFO");
    for (unsigned i = 0; i < 7; ++i)
        check(s.model().current(static_cast<Channel>(i), n) == manual[i], "exact seven values");
    auto values = manual;
    values[0] = 0;
    s.setManual(values);
    check(s.model().current(Channel::Rpm, n) == 3500, "control not measurement");
    advance(s, n, 200);
    check(s.model().current(Channel::Rpm, n) == 0, "valid zero");
    std::array<Quality, 7> q;
    q.fill(Quality::Valid);
    q[2] = Quality::Unsupported;
    q[3] = Quality::Invalid;
    s.setQualities(q);
    advance(s, n, 250);
    check(s.model().channels()[2].quality == Quality::Unsupported && !s.model().current(Channel::Coolant, n),
          "unsupported");
    check(s.model().channels()[3].quality == Quality::Invalid && !s.model().current(Channel::Intake, n),
          "invalid");
    check(rx > tx && tx > 5, "fragmented raw path");
    for (int i = 0; i < 10000; ++i) {
        values[0] = i % 12000;
        s.setManual(values);
    }
    advance(s, n, 300);
    check(s.model().current(Channel::Rpm, n) == 9999, "bounded latest desired state");
}
void freshnessFaults() {
    InMemoryTransport t;
    Session s(t);
    Time n = 0;
    s.start(n);
    advance(s, n, 300);
    s.setFaults({true, 0, false, false});
    advance(s, n, 500);
    auto last = s.model().channels()[0].lastValid;
    advance(s, n, 1200);
    check(s.model().channels()[0].quality == Quality::Stale && s.model().current(Channel::Rpm, n),
          "stale temporarily displayed");
    advance(s, n, 2100);
    check(!s.model().current(Channel::Rpm, n) && s.model().channels()[0].lastValid == last,
          "hide old data without refresh");
    s.setFaults({});
    advance(s, n, 600);
    check(s.model().channels()[0].quality == Quality::Valid, "restore command reaches silent endpoint");
    auto corrupt = s.stats().corrupt;
    s.setFaults({false, 0, true, false});
    advance(s, n, 700);
    check(s.stats().corrupt > corrupt && s.state() == SessionState::Running,
          "once CRC leaves ACK intact and recovers");
    auto timeouts = s.stats().timeouts;
    s.setFaults({false, 0, false, true});
    advance(s, n, 800);
    check(s.stats().timeouts > timeouts && s.model().channels()[0].quality == Quality::Valid,
          "truncate timeout and resync");
    s.setFaults({false, 700, false, false});
    advance(s, n, 5000);
    check(!s.model().current(Channel::Rpm, n) && s.stats().ignored > 0, "late frames cannot refresh");
    s.setFaults({});
    advance(s, n, 1500);
    check(s.model().current(Channel::Rpm, n).has_value(), "recover delayed stream");
}
void lifecycleLoad() {
    InMemoryTransport t, other;
    Session s(t);
    Time n = 0;
    for (unsigned i = 0; i < 200; ++i) {
        s.setTransport(i % 2 ? t : other);
        auto old = s.id();
        s.start(n);
        check(s.id() != old, "new identity");
        advance(s, n, 50);
        s.stop(n);
        check(!s.model().current(Channel::Rpm, n) && t.pendingDeliveries() == 0 &&
                  other.pendingDeliveries() == 0,
              "stop cancels and clears");
    }
    s.setTransport(t);
    s.start(n);
    advance(s, n, 600000);
    check(s.stats().accepted > 5900 && t.pendingDeliveries() < 32, "ten minutes bounded polling");
    s.stop(n);
    t.emulator().faults().delayMs = 1000000;
    s.start(n);
    advance(s, n, 100000);
    check(s.state() == SessionState::Faulted && !s.error().empty(), "overflow visibly terminates");
}
void responseMatching() {
    Fake wire;
    Session session(wire);
    session.start(0); session.tick(0);
    std::string profile="synthetic-demo-v1";
    std::vector<std::uint8_t> hello{1}; hello.insert(hello.end(),profile.begin(),profile.end());
    wire.response(HelloResponse,hello,1); session.tick(1);
    wire.response(GetDeviceInfo|0x80,{1,1,0,2,0,15,0,'d','e','s','k','t','o','p','-','e','m','u','l','a','t','o','r'},2);
    session.tick(2);
    const auto request=wire.requests.back();
    Sample sample; sample.values={9999,42.5,80,-12.3,25,100,14.2}; sample.qualities.fill(Quality::Valid);
    auto payload=encodeSnapshot(sample);
    wire.cb.received(encode({SnapshotResponse,request.session+1,request.request,payload}),3);
    wire.cb.received(encode({SnapshotResponse,request.session,request.request+1,payload}),3);
    check(session.stats().accepted==0,"foreign session/request cannot update");
    wire.response(SnapshotResponse,{1},3);
    check(session.stats().corrupt>0 && session.stats().accepted==0,"invalid snapshot cannot refresh");
    auto response=encode({SnapshotResponse,request.session,request.request,payload});
    wire.cb.received(response,4); wire.cb.received(response,5);
    check(session.stats().accepted==1 && session.model().current(Channel::Rpm,5)==9999,"only first valid matching response accepted");
    session.tick(102);
    wire.response(SnapshotResponse,payload,402);
    check(session.stats().accepted==1,"response exactly at deadline rejected");
    session.tick(402);
    wire.cb.received(response,403);
    check(session.stats().accepted==1 && session.stats().ignored>=4,"old request cannot refresh new pending");
}
void deadlinesAndGuards() {
    Fake t;
    std::uint32_t id = 100;
    SessionSettings config;
    config.bootDelayMs = 2000;
    config.handshakeDeadlineMs = 4000;
    Session s(t, config, {}, [&] { return ++id; });
    Time n = 0;
    s.start(n);
    auto old = t.cb;
    advance(s, n, 1999);
    check(t.requests.empty() && s.state() == SessionState::BootWaiting, "nonblocking boot phase");
    advance(s, n, 1);
    check(t.requests.size() == 1, "hello after boot");
    advance(s, n, 1000);
    check(s.state() == SessionState::Faulted && t.requests.size() == 3, "bounded hello no reopen");
    s.start(n);
    auto current = s.id();
    old.opened(n);
    old.error("old error", n);
    old.received(encode({SnapshotResponse, current, 1, {}}), n);
    check(s.id() == current && s.state() == SessionState::BootWaiting, "all old callbacks ignored");
    s.stop(n);
    s.setSettings({});
    t.drain = false;
    s.start(n);
    advance(s, n, 600);
    check(s.state() == SessionState::Faulted && s.error().find("TX") != std::string::npos,
          "TX zero progress deadline");
    auto copied = t.cb;
    {
        Fake local;
        Session temporary(local);
        temporary.start(0);
        copied = local.cb;
    }
    copied.error("after destruction", n); // ASan-friendly lifetime guard
    Fake bad;
    Session incompatible(bad);
    incompatible.start(0);
    incompatible.tick(0);
    bad.response(HelloResponse, {1, 'b', 'a', 'd'}, 1);
    check(incompatible.state() == SessionState::Faulted && incompatible.stats().accepted == 0,
          "wrong profile stops polling");
    Fake info;
    Session wrongEndpoint(info);
    wrongEndpoint.start(0);
    wrongEndpoint.tick(0);
    std::string profile = "synthetic-demo-v1";
    std::vector<std::uint8_t> hello{1};
    hello.insert(hello.end(), profile.begin(), profile.end());
    info.response(HelloResponse, hello, 1);
    wrongEndpoint.tick(1);
    info.response(GetDeviceInfo | 0x80, {1, 2, 0, 2, 0, 15, 0, 'x'}, 2);
    check(wrongEndpoint.state() == SessionState::Faulted, "wrong endpoint");
    Fake late;
    late.drain = false;
    SessionSettings txSettings;
    txSettings.txTimeoutMs = 50;
    Session lateTx(late, txSettings);
    lateTx.start(0);
    lateTx.tick(0);
    late.queued = 0;
    late.cb.sent(50);
    check(lateTx.state() == SessionState::Faulted, "sent at TX deadline cannot revive expired request");
    Fake total;
    SessionSettings totalSettings;
    totalSettings.handshakeDeadlineMs = 100;
    Session totalDeadline(total, totalSettings);
    totalDeadline.start(0);
    totalDeadline.tick(0);
    total.response(HelloResponse, hello, 1);
    totalDeadline.tick(1);
    total.response(GetDeviceInfo | 0x80, {1,   1,   0,   2,   0,   15,  0,   'd', 'e', 's', 'k', 't',
                                          'o', 'p', '-', 'e', 'm', 'u', 'l', 'a', 't', 'o', 'r'},
                   100);
    check(totalDeadline.state() == SessionState::Faulted, "INFO at total handshake deadline rejected");
    Fake reentrant;
    Session cancelled(reentrant);
    cancelled.onRaw = [&](const RawEvent &e) {
        if (e.kind == "TX_QUEUED" || e.kind == "stop")
            cancelled.stop(e.time);
    };
    cancelled.start(0);
    cancelled.tick(0);
    check(reentrant.requests.empty() && cancelled.state() == SessionState::Stopped,
          "stop from TX event prevents old send and recursive stop");
    cancelled.onRaw = [&](const RawEvent &e) {
        if (e.kind == "start")
            cancelled.stop(e.time);
    };
    cancelled.start(1);
    check(!reentrant.open && cancelled.state() == SessionState::Stopped,
          "stop during start cannot leave invisible open transport");
    InMemoryTransport memory;
    unsigned oldRx = 0, newRx = 0;
    TransportCallbacks initial;
    initial.opened = [](Time) {};
    initial.sent = [](Time) {};
    initial.raw = [&](const std::string &k, auto, Time time) {
        if (k == "RX") {
            TransportCallbacks next;
            next.received = [&](auto, Time) { ++newRx; };
            memory.start(next, time);
        }
    };
    initial.received = [&](auto, Time) { ++oldRx; };
    memory.start(initial, 0);
    memory.send(encode({Hello, 55, 1, {}}), 0);
    memory.tick(0);
    check(oldRx == 0 && newRx == 0, "memory old RX cannot cross reconnect from raw callback");
    Fake ids;
    Session repeated(ids, {}, {}, [] { return 42; });
    repeated.start(0);
    repeated.start(1);
    check(repeated.state() == SessionState::Faulted, "reject repeated injected identity");
}
} // namespace
int main() {
    try {
        pipeline();
        freshnessFaults();
        lifecycleLoad();
        deadlinesAndGuards();
        responseMatching();
        std::cout << "session: pipeline, faults, lifecycle/load, deadlines/guards passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
