#include "../firmware/nano_dlc_bridge_bench/endpoint.hpp"
#include "../firmware/nano_dlc_responder_bench/endpoint.hpp"
#include "../firmware/shared/physical_dlc_port.hpp"
#include "protocol/protocol.hpp"
#include "support/one_wire_line.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
namespace b = hd_bridge;
namespace n = hd_bench;
using Bytes = std::vector<std::uint8_t>;
unsigned assertions = 0;
void check(bool value, const char *what, int line) {
    ++assertions;
    if (!value)
        throw std::runtime_error(std::to_string(line) + ": " + what);
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)
const Bytes Rpm{2, 5, 0x20, 5, 0, 2, 0xd9}, Ect{2, 4, 0x20, 5, 0x10, 1, 0xca},
    Tps{2, 4, 0x20, 5, 0x14, 1, 0xc6};
const Bytes RpmA{0, 5, 9, 0xc3, 0x2f}, RpmB{0, 5, 4, 0xe1, 0x16}, EctA{0, 4, 0x40, 0xbc};
template <class E> std::vector<hd::Frame> drain(E &endpoint) {
    hd::Parser parser;
    std::vector<hd::Frame> frames;
    std::array<std::uint8_t, 7> bytes{};
    while (endpoint.available()) {
        const auto n = endpoint.read(bytes.data(), bytes.size());
        for (auto frame : parser.feed(std::span(bytes).first(n)))
            frames.push_back(std::move(frame));
    }
    CHECK(parser.errors() == 0 && parser.buffered() == 0);
    return frames;
}
struct Rig {
    hd_test::PairLine line;
    hd_test::DriverPort first, second;
    n::PhysicalDlcPort<hd_test::DriverPort> bridgePort, responderPort;
    n::BridgeEndpoint bridge;
    n::ResponderEndpoint responder;
    std::uint32_t bridgeId = 0, responderId = 0;
    std::vector<hd::Frame> bridgeFrames, responderFrames;
    explicit Rig(std::uint32_t ticks = 0)
        : line(ticks), first(line.first, line), second(line.second, line), bridgePort(first),
          responderPort(second), bridge(bridgePort), responder(responderPort) {}
    template <class E>
    std::vector<hd::Frame> send(E &endpoint, std::uint32_t &id, std::uint8_t command, const Bytes &payload) {
        for (auto byte : hd::encode({command, 0x1234, ++id, payload}))
            endpoint.receive(byte, line.millis());
        endpoint.tick(line.millis());
        line.service();
        return drain(endpoint);
    }
    std::vector<hd::Frame> bc(std::uint8_t command, const Bytes &payload) {
        return send(bridge, bridgeId, command, payload);
    }
    std::vector<hd::Frame> rc(std::uint8_t command, const Bytes &payload) {
        return send(responder, responderId, command, payload);
    }
    void advance(unsigned milliseconds) {
        for (unsigned i = 0; i < milliseconds; ++i) {
            line.advanceTo(line.now() + 2000u);
            bridge.tick(line.millis());
            responder.tick(line.millis());
            line.service();
            for (auto f : drain(bridge))
                bridgeFrames.push_back(std::move(f));
            for (auto f : drain(responder))
                responderFrames.push_back(std::move(f));
        }
    }
    void clearFrames() {
        bridgeFrames.clear();
        responderFrames.clear();
    }
    void hello() {
        auto a = bc(b::Hello, {2, 2}), c = rc(b::Hello, {2, 2});
        CHECK(a.size() == 1 && a[0].type == b::HelloInfo && a[0].payload[2] == n::BackendBridge);
        CHECK(std::string(a[0].payload.begin() + 15, a[0].payload.end()) == n::BridgeIdentity);
        CHECK(c.size() == 1 && c[0].type == b::HelloInfo && c[0].payload[2] == n::BackendResponder);
        CHECK(std::string(c[0].payload.begin() + 15, c[0].payload.end()) == n::ResponderIdentity);
        CHECK(bridgePort.txBytes() == 0 && responderPort.txBytes() == 0);
    }
    void boundary() {
        auto q = rc(n::Quiesce, {2});
        CHECK(q.size() == 1 && q[0].payload[6] == b::Ok);
        const auto gen = responder.generation();
        CHECK(gen > 0 && responder.quiescent());
        advance(20);
        Bytes p{2, 0, 0, 0, 0};
        b::store32(p.data() + 1, gen);
        auto fresh = bc(b::NewExperiment, p);
        CHECK(fresh.size() == 1 && fresh[0].payload[6] == b::Ok);
        auto arm = rc(n::Arm, p);
        CHECK(arm.size() == 1 && arm[0].payload[6] == b::Ok && responder.armed());
        CHECK(bc(b::Initialize, {2}).empty());
        clearFrames();
        advance(330);
        CHECK(bridgeFrames.size() == 1 && bridgeFrames[0].type == b::Result &&
              bridgeFrames[0].payload[5] == b::Ok);
        CHECK(b::load16(bridgeFrames[0].payload.data() + 11) >= 11);
        CHECK(b::load16(bridgeFrames[0].payload.data() + 13) == 300);
        clearFrames();
    }
    void config(unsigned scenario, unsigned fault = 0, unsigned delay = 0, unsigned gap = 0) {
        auto a = rc(b::Configure, {2, std::uint8_t(scenario), std::uint8_t(fault), std::uint8_t(delay),
                                   std::uint8_t(delay >> 8), std::uint8_t(gap), std::uint8_t(gap >> 8)});
        CHECK(a.size() == 1 && a[0].type == b::Ack && a[0].payload[6] == b::Ok);
    }
    hd::Frame read(const Bytes &request, unsigned elapsed = 230) {
        clearFrames();
        CHECK(bc(b::Execute, request).empty());
        advance(elapsed);
        CHECK(!bridgeFrames.empty() && bridgeFrames[0].type == b::Result);
        return bridgeFrames[0];
    }
};
Bytes raw(const hd::Frame &frame) {
    const auto offset = b::ResultHeader + frame.payload[9];
    CHECK(frame.payload.size() == std::size_t(offset + frame.payload[10]));
    return {frame.payload.begin() + offset, frame.payload.end()};
}
void strictIdentityAndGates() {
    Rig r;
    auto old = r.bc(b::Hello, {1, 1});
    CHECK(old.size() == 1 && old[0].payload[6] == b::PolicyDenied);
    auto wrong = r.rc(b::Hello, {1, 1});
    CHECK(wrong.size() == 1 && wrong[0].payload[6] == b::PolicyDenied);
    r.hello();
    auto noBoundary = r.bc(b::Initialize, {2});
    CHECK(noBoundary[0].payload[6] == b::NeedsNewExperiment);
    auto noPeer = r.bc(b::NewExperiment, {2});
    CHECK(noPeer[0].payload[6] == b::BadPayload);
    auto notArmed = r.rc(n::Arm, {2, 1, 0, 0, 0});
    CHECK(notArmed[0].payload[6] == b::NeedsNewExperiment);
    CHECK(r.bridgePort.txBytes() == 0 && r.responderPort.txBytes() == 0);
    r.boundary();
    auto config = r.bc(b::Configure, {2, 0, 0, 0, 0, 0, 0});
    CHECK(config[0].payload[6] == b::PolicyDenied);
    const auto count = r.bridgePort.txBytes();
    for (unsigned address = 0; address < 256; ++address) {
        if (address == 0 || address == 0x10 || address == 0x14)
            continue;
        const auto checksum = std::uint8_t(0u - 0x20u - 5u - address - 1u);
        auto denied = r.bc(b::Execute, {2, 4, 0x20, 5, std::uint8_t(address), 1, checksum});
        CHECK(denied.size() == 1 && denied[0].payload[6] == b::PolicyDenied);
    }
    CHECK(r.bridgePort.txBytes() == count);
}
void physicalBytesFixturesAndFaults() {
    Rig r;
    r.hello();
    r.boundary();
    auto a = r.read(Rpm);
    CHECK(a.payload[5] == b::Ok && raw(a) == RpmA);
    CHECK(b::load16(a.payload.data() + 11) >= 5 && b::load16(a.payload.data() + 13) == 200);
    CHECK(r.line.first.counters().echoBytes >= 16 && r.line.second.counters().rxBytes >= 16);
    r.config(1);
    auto bframe = r.read(Rpm);
    CHECK(bframe.payload[5] == b::Ok && raw(bframe) == RpmB);
    r.config(3);
    auto boundary = r.read(Tps);
    CHECK(raw(boundary) == Bytes({0, 4, 0x18, 0xe4}));
    r.config(0, b::Checksum);
    auto bad = r.read(Ect);
    CHECK(bad.payload[5] == b::DlcChecksum);
    CHECK(r.bridge.state() == b::Faulted && r.line.high());
    const auto count = r.bridgePort.txBytes();
    auto blocked = r.bc(b::Execute, Tps);
    CHECK(blocked[0].payload[6] == b::NeedsNewExperiment);
    CHECK(r.bridgePort.txBytes() == count);
    r.boundary();
    auto recovered = r.read(Ect);
    CHECK(recovered.payload[5] == b::Ok && raw(recovered) == EctA);
    CHECK(r.responder.counters().lineErrors == 0);
    // Timer1's 0.5us clock wraps while the engine retains its separate MCU millis.
    Rig wrap(0xfffff000u);
    wrap.hello();
    wrap.boundary();
    auto wr = wrap.read(Rpm);
    CHECK(wr.payload[5] == b::Ok && raw(wr) == RpmA);
}
void externalRecoveryAndLate() {
    Rig r;
    r.hello();
    r.boundary();
    r.config(0, b::Delay, 400);
    auto timeout = r.read(Ect);
    CHECK(timeout.payload[5] == b::DlcTotalTimeout);
    auto stop = r.bc(b::Abort, {2});
    CHECK(stop.size() == 1 && stop[0].payload[6] == b::Ok);
    const auto count = r.bridgePort.txBytes();
    auto blocked = r.bc(b::Execute, Tps);
    CHECK(blocked[0].payload[6] == b::NeedsNewExperiment);
    r.clearFrames();
    r.advance(220);
    Bytes late;
    for (const auto &f : r.bridgeFrames) {
        CHECK(f.type == b::Event && f.request == 0);
        late.insert(late.end(), f.payload.begin() + b::EventHeader, f.payload.end());
    }
    CHECK(late == EctA && r.bridgePort.txBytes() == count);
    r.boundary();
    r.config(0, b::Delay, 1000);
    CHECK(r.read(Ect).payload[5] == b::DlcTotalTimeout);
    auto q = r.rc(n::Quiesce, {2});
    CHECK(q.size() == 1 && r.responder.counters().canceled == 1);
    r.clearFrames();
    r.advance(1100);
    CHECK(r.bridgeFrames.empty());
    // Resetting only bridge never cancels the independently scheduled responder.
    r.boundary();
    r.config(0, b::Delay, 400);
    CHECK(r.read(Ect).payload[5] == b::DlcTotalTimeout);
    r.bridge.reset(r.line.millis());
    r.clearFrames();
    r.advance(230);
    CHECK(!r.bridgeFrames.empty() && r.responder.counters().canceled == 1);
    CHECK(r.bridge.state() == b::Faulted);
}
void quiesceWaitsForStopAndNoPeer() {
    Rig r;
    r.hello();
    r.boundary();
    CHECK(r.bc(b::Execute, Rpm).empty());
    // Request5.2ms + turnaround2ms: at9ms the responder is on the physical line.
    r.advance(9);
    CHECK(!r.responderPort.txIdle(r.line.millis()));
    auto q = r.rc(n::Quiesce, {2});
    CHECK(q.empty());
    const auto generation = r.responder.generation();
    r.advance(10);
    CHECK(r.responderFrames.size() == 1 && r.responderFrames[0].type == b::Ack);
    CHECK(r.responderFrames[0].payload[5] == n::Quiesce && r.responder.generation() == generation + 1);
    CHECK(r.responderPort.txIdle(r.line.millis()) && r.line.high());
    // Responder reset leaves it disarmed: bridge has no internal virtual fallback.
    r.advance(220);
    r.clearFrames();
    r.responder.reset(r.line.millis());
    auto result = r.read(Rpm);
    CHECK(result.payload[5] == b::DlcTotalTimeout);
    CHECK(r.responderPort.txBytes() == 5);
    // Both renewed identities permit reset peer's fresh generation1, but still
    // require QUIESCE/drain/NEW/ARM before another signal request.
    CHECK(r.bc(b::Hello, {2, 2})[0].type == b::HelloInfo);
    CHECK(r.rc(b::Hello, {2, 2})[0].type == b::HelloInfo);
    r.boundary();
    CHECK(r.read(Rpm).payload[5] == b::Ok);
}
void driverErrorsReachEngine() {
    Rig r;
    r.hello();
    r.boundary();
    CHECK(r.bc(b::Execute, Rpm).empty());
    r.line.forceLow(true);
    r.advance(3);
    CHECK(!r.bridgeFrames.empty());
    const auto status = r.bridgeFrames[0].payload[5];
    CHECK(status == b::DlcCollision || status == b::DlcLine || status == b::DlcFraming);
    CHECK(r.bridgeFrames[0].payload[9] == 0); // No full stop bit completed; queued bytes are not actual TX.
    CHECK(r.bridge.state() == b::Faulted && !r.line.first.drivingLow());
    r.line.forceLow(false);
    r.advance(20);
    CHECK(r.line.high());

    Rig boundary;
    boundary.hello();
    CHECK(boundary.rc(n::Quiesce, {2})[0].payload[6] == b::Ok);
    boundary.line.forceLow(true);
    boundary.advance(3);
    boundary.line.forceLow(false);
    boundary.advance(20);
    CHECK(boundary.line.high() && boundary.responder.counters().lineErrors > 0);
    // An error arriving after the quiescence ACK needs a new boundary; an idle
    // pin cannot silently turn a sticky driver fault into an armed responder.
    CHECK(boundary.rc(n::Arm, {2, 1, 0, 0, 0})[0].payload[6] == b::NeedsNewExperiment);
    CHECK(!boundary.responder.armed());
    CHECK(boundary.rc(n::Quiesce, {2})[0].payload[6] == b::Ok);
    boundary.advance(20);
    CHECK(boundary.rc(n::Arm, {2, 2, 0, 0, 0})[0].payload[6] == b::Ok);
}
void faultsDuplicatesAndDiagnostics() {
    const std::array<unsigned, 8> faults{b::Silent,   b::Gap,       b::Header, b::Length,
                                         b::Checksum, b::Truncated, b::Noise,  b::Trailing};
    const std::array<unsigned, 8> expected{b::DlcTotalTimeout, b::DlcInterbyteTimeout, b::DlcHeader,
                                           b::DlcLength,       b::DlcChecksum,         b::DlcInterbyteTimeout,
                                           b::DlcHeader,       b::DlcTrailing};
    Rig r;
    r.hello();
    for (std::size_t i = 0; i < faults.size(); ++i) {
        r.boundary();
        r.config(0, faults[i], 0, 60);
        auto f = r.read(Ect);
        CHECK(f.payload[5] == expected[i]);
        CHECK(r.line.high() && r.bridge.state() == b::Faulted);
    }
    r.boundary();
    r.config(0, b::Checksum);
    const auto duplicateId = r.responderId;
    CHECK(r.read(Rpm).payload[5] == b::DlcChecksum);
    // Same outer CONFIG request never re-arms a consumed one-shot DLC fault.
    const auto before = r.responder.generation();
    for (auto byte : hd::encode({b::Configure, 0x1234, duplicateId, {2, 0, b::Checksum, 0, 0, 0, 0}}))
        r.responder.receive(byte, r.line.millis());
    auto duplicate = drain(r.responder);
    CHECK(duplicate.size() == 1 && duplicate[0].payload[6] == b::Ok);
    CHECK(r.responder.generation() == before);
    r.boundary();
    CHECK(r.read(Rpm).payload[5] == b::Ok);
    auto bridgeDiag = r.bc(b::Diagnostics, {2}), responderDiag = r.rc(b::Diagnostics, {2});
    CHECK(bridgeDiag.size() == 1 && bridgeDiag[0].payload.size() == 52);
    CHECK(responderDiag.size() == 1 && responderDiag[0].payload.size() == 60);
    CHECK(b::load16(bridgeDiag[0].payload.data() + 32) > 0);    // completed driver TX counter
    CHECK(b::load16(responderDiag[0].payload.data() + 38) > 0); // physical RX counter
    const auto tx = r.responderPort.txBytes();
    for (unsigned i = 0; i < 10000; ++i)
        r.responder.receive(std::uint8_t(i), r.line.millis());
    CHECK(r.responder.buffered() <= b::MaxFrame && r.responder.available() <= b::TxCapacity);
    CHECK(r.responderPort.txBytes() == tx && r.responder.counters().parserErrors > 0);
}
} // namespace
int main() {
    try {
        strictIdentityAndGates();
        physicalBytesFixturesAndFaults();
        externalRecoveryAndLate();
        quiesceWaitsForStopAndNoPeer();
        driverErrorsReachEngine();
        faultsDuplicatesAndDiagnostics();
        std::cout << "Two-Nano embedded bit-line tests PASS; assertions=" << assertions << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
