#include "endpoint.hpp"
#include "protocol/protocol.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace b = hd_bridge;
using Bytes = std::vector<std::uint8_t>;
void require(bool value, const char *expression, int line) {
    if (!value)
        throw std::runtime_error(std::to_string(line) + ": " + expression);
}
#define CHECK(...) require(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
constexpr std::uint32_t Session = 0x01020304;
const Bytes HelloGolden{0xa5, 0x5a, 1, 0x30, 4, 3, 2, 1, 1, 0, 0, 0, 2, 1, 1, 0xb2, 0xa0};
const Bytes NewGolden{0xa5, 0x5a, 1, 0x31, 4, 3, 2, 1, 2, 0, 0, 0, 1, 1, 0x64, 0xbf};
const Bytes ExecuteGolden{0xa5, 0x5a, 1, 0x33, 4,    3, 2, 1, 5,    0,    0,
                          0,    7,    1, 5,    0x20, 5, 0, 2, 0xd9, 0xf7, 0xcc};
const Bytes ResultGolden{0xa5, 0x5a, 0x01, 0xb2, 0x04, 0x03, 0x02, 0x01, 0x05, 0, 0,    0,    0x1b, 1,
                         1,    0,    0,    0,    0,    0x33, 0,    2,    5,    5, 0,    0,    0xc8, 0,
                         2,    0,    0x20, 5,    0,    2,    0xd9, 0,    5,    9, 0xc3, 0x2f, 0x81, 0x9e};
const Bytes RpmA{0, 5, 9, 0xc3, 0x2f}, RpmB{0, 5, 4, 0xe1, 0x16};
const Bytes EctA{0, 4, 0x40, 0xbc}, EctB{0, 4, 0x20, 0xdc};
const Bytes TpsA{0, 4, 0x58, 0xa4}, TpsB{0, 4, 0xae, 0x4e};
const Bytes Rpm{1, 5, 0x20, 5, 0, 2, 0xd9}, Ect{1, 4, 0x20, 5, 0x10, 1, 0xca},
    Tps{1, 4, 0x20, 5, 0x14, 1, 0xc6};
void feed(b::BridgeEndpoint &endpoint, const Bytes &bytes, std::uint32_t now) {
    for (auto byte : bytes) {
        endpoint.receive(byte, now);
        CHECK(endpoint.buffered() <= b::MaxFrame && endpoint.available() <= b::TxCapacity);
    }
    endpoint.tick(now);
}
std::vector<hd::Frame> drain(b::BridgeEndpoint &endpoint, std::size_t chunk = 1) {
    hd::Parser parser;
    std::vector<hd::Frame> result;
    std::array<std::uint8_t, b::MaxFrame> bytes{};
    while (endpoint.available()) {
        auto count = endpoint.read(bytes.data(), chunk);
        CHECK(count > 0 && count <= chunk);
        for (auto frame : parser.feed(std::span(bytes).first(count)))
            result.push_back(std::move(frame));
    }
    CHECK(parser.errors() == 0);
    return result;
}
hd::Frame exchange(b::BridgeEndpoint &endpoint, std::uint8_t command, std::uint32_t id, const Bytes &payload,
                   std::uint32_t now) {
    feed(endpoint, hd::encode({command, Session, id, payload}), now);
    auto frames = drain(endpoint);
    CHECK(frames.size() == 1 && frames[0].session == Session && frames[0].request == id);
    return frames[0];
}
void status(const hd::Frame &frame, std::uint8_t type, std::uint8_t value) {
    CHECK(frame.type == type && frame.payload.size() == b::AckSize && frame.payload[6] == value);
}
void setup(b::BridgeEndpoint &endpoint, std::uint32_t base = 0, unsigned scenario = 0, unsigned fault = 0,
           std::uint16_t delay = 0, std::uint16_t gap = 0) {
    endpoint.reset(base);
    const auto hello = exchange(endpoint, b::Hello, 1, {1, 1}, base);
    CHECK(hello.type == b::HelloInfo && hello.payload[0] == 1 && hello.payload[1] == 1);
    CHECK(hello.payload[2] == b::BackendVirtual && hello.payload[3] == 0);
    CHECK(std::string(hello.payload.begin() + 15, hello.payload.end()) == b::Identity);
    status(exchange(endpoint, b::NewExperiment, 2, {1}, base), b::Ack, b::Ok);
    Bytes config{1,
                 std::uint8_t(scenario),
                 std::uint8_t(fault),
                 std::uint8_t(delay),
                 std::uint8_t(delay >> 8),
                 std::uint8_t(gap),
                 std::uint8_t(gap >> 8)};
    status(exchange(endpoint, b::Configure, 3, config, base), b::Ack, b::Ok);
    feed(endpoint, hd::encode({b::Initialize, Session, 4, {1}}), base);
    CHECK(endpoint.available() == 0 && endpoint.state() == b::Initializing);
    endpoint.tick(base + 299);
    CHECK(endpoint.available() == 0);
    endpoint.tick(base + 300);
    auto frames = drain(endpoint);
    CHECK(frames.size() == 1 && frames[0].type == b::Result && frames[0].payload[5] == b::Ok);
    CHECK(frames[0].payload[6] == b::Initialize && frames[0].payload[9] == 11);
    CHECK(b::load16(frames[0].payload.data() + 13) == 300 && endpoint.state() == b::Ready);
}
std::vector<hd::Frame> run(b::BridgeEndpoint &endpoint, std::uint32_t from, unsigned count) {
    std::vector<hd::Frame> frames;
    for (unsigned i = 0; i <= count; ++i) {
        endpoint.tick(from + i);
        for (auto &frame : drain(endpoint))
            frames.push_back(std::move(frame));
    }
    return frames;
}
Bytes response(const hd::Frame &frame) {
    CHECK(frame.type == b::Result && frame.payload.size() >= b::ResultHeader);
    const auto offset = std::size_t(b::ResultHeader + frame.payload[9]);
    CHECK(frame.payload.size() == offset + frame.payload[10]);
    return {frame.payload.begin() + offset, frame.payload.end()};
}
void goldensAndIdentity() {
    CHECK(hd::encode({b::Hello, Session, 1, {1, 1}}) == HelloGolden);
    CHECK(hd::encode({b::NewExperiment, Session, 2, {1}}) == NewGolden);
    CHECK(hd::encode({b::Execute, Session, 5, Rpm}) == ExecuteGolden);
    b::BridgeEndpoint endpoint;
    feed(endpoint, HelloGolden, 0);
    auto frames = drain(endpoint);
    CHECK(frames.size() == 1 && frames[0].type == b::HelloInfo);
    const auto info = frames[0];
    feed(endpoint, HelloGolden, 1);
    frames = drain(endpoint);
    CHECK(frames.size() == 1 && frames[0].payload == info.payload && endpoint.counters().duplicates == 1);
    status(exchange(endpoint, b::Hello, 2, {2, 1}, 2), b::Error, b::PolicyDenied);
    status(exchange(endpoint, 1, 3, {}, 3), b::Error, b::BadCommand); // M1 HELLO
    status(exchange(endpoint, b::Execute, 3, Rpm, 3), b::Error, b::NeedsNewExperiment);
    CHECK(endpoint.dlcTxBytes() == 0);
    Bytes corrupt = HelloGolden;
    corrupt.back() ^= 1;
    feed(endpoint, corrupt, 4);
    CHECK(drain(endpoint).empty());
    CHECK(endpoint.counters().parserErrors > 0 && endpoint.dlcTxBytes() == 0);
    // Coalesced framed commands, no hidden frame-sized RX requirement.
    endpoint.reset(0);
    Bytes joined = HelloGolden;
    joined.insert(joined.end(), NewGolden.begin(), NewGolden.end());
    feed(endpoint, joined, 0);
    frames = drain(endpoint, 7);
    CHECK(frames.size() == 2 && frames[1].type == b::Ack);
    for (unsigned i = 0; i < 10000; ++i)
        endpoint.receive(std::uint8_t(i), 0);
    CHECK(endpoint.buffered() <= b::MaxFrame && endpoint.dlcTxBytes() == 0);
}
void fixturesAndDuplicates() {
    for (unsigned scenario : {0u, 1u, 3u}) {
        b::BridgeEndpoint endpoint;
        setup(endpoint, 0, scenario);
        std::uint32_t id = 5, now = 301;
        for (const auto &request : {Rpm, Ect, Tps}) {
            const auto wire = hd::encode({b::Execute, Session, id, request});
            const auto txBefore = endpoint.dlcTxBytes();
            feed(endpoint, wire, now);
            feed(endpoint, wire, now + 1);
            CHECK(endpoint.dlcTxBytes() == txBefore + 5 && endpoint.available() == 0);
            auto frames = run(endpoint, now + 2, 197);
            CHECK(frames.empty());
            endpoint.tick(now + 200);
            frames = drain(endpoint);
            CHECK(frames.size() == 1 && frames[0].payload[5] == b::Ok && frames[0].request == id);
            const auto expected = scenario == 3    ? (request == Rpm   ? Bytes{0, 5, 0xff, 0xff, 0xfd}
                                                      : request == Ect ? Bytes{0, 4, 0xff, 0xfd}
                                                                       : Bytes{0, 4, 0x18, 0xe4})
                                  : request == Rpm ? (scenario ? RpmB : RpmA)
                                  : request == Ect ? (scenario ? EctB : EctA)
                                                   : (scenario ? TpsB : TpsA);
            CHECK(response(frames[0]) == expected);
            if (scenario == 0 && request == Rpm)
                CHECK(hd::encode(frames[0]) == ResultGolden);
            CHECK(Bytes(frames[0].payload.begin() + 17, frames[0].payload.begin() + 22) ==
                  Bytes(request.begin() + 2, request.end()));
            CHECK(b::load16(frames[0].payload.data() + 11) == 0 &&
                  b::load16(frames[0].payload.data() + 13) == 200);
            CHECK(b::load16(frames[0].payload.data() + 15) == 2);
            feed(endpoint, wire, now + 201);
            auto duplicate = drain(endpoint);
            CHECK(duplicate.size() == 1 && duplicate[0].payload == frames[0].payload &&
                  endpoint.dlcTxBytes() == txBefore + 5);
            ++id;
            now += 202;
        }
        status(exchange(endpoint, b::Execute, 5, Rpm, now), b::Error, b::StaleRequest);
    }
}
void readOnlyPolicy() {
    b::BridgeEndpoint endpoint;
    setup(endpoint);
    auto tx = endpoint.dlcTxBytes();
    std::uint32_t id = 5;
    for (unsigned address = 0; address < 256; ++address) {
        if (address == 0 || address == 0x10 || address == 0x14)
            continue;
        const auto check = std::uint8_t(0u - (0x20u + 5u + address + 1u));
        status(exchange(endpoint, b::Execute, id++, {1, 4, 0x20, 5, std::uint8_t(address), 1, check}, 301),
               b::Error, b::PolicyDenied);
        CHECK(endpoint.dlcTxBytes() == tx);
    }
    const std::vector<Bytes> bad{{1, 5, 0x21, 5, 0, 2, 0xd8},
                                 {1, 5, 0x20, 5, 0, 2, 0xd8},
                                 {1, 5, 0x20, 5, 0, 0, 0xdb},
                                 {1, 4, 0x20, 5, 0, 2, 0xd9},
                                 {2, 5, 0x20, 5, 0, 2, 0xd9},
                                 {1, 5, 0x20, 5, 0, 2, 0xd9, 0},
                                 {1, 5, 0x20, 5, 0, 2, 0xd9, 0x20, 5, 0, 2, 0xd9}};
    for (const auto &payload : bad) {
        const auto frame = exchange(endpoint, b::Execute, id++, payload, 301);
        CHECK(frame.type == b::Error && endpoint.dlcTxBytes() == tx);
    }
    status(exchange(endpoint, b::Configure, id++, {1, 0, 10, 0, 0, 0, 0}, 301), b::Error, b::BadPayload);
    status(exchange(endpoint, b::Configure, id++, {1, 2, 0, 0, 0, 0, 0}, 301), b::Error, b::BadPayload);
    status(exchange(endpoint, b::Initialize, id++, {1, 0x12}, 301), b::Error, b::BadPayload);
    CHECK(endpoint.dlcTxBytes() == tx);
}
void faultsAndLateBytes() {
    const std::array<unsigned, 8> faults{b::Silent,   b::Gap,       b::Header, b::Length,
                                         b::Checksum, b::Truncated, b::Noise,  b::Trailing};
    const std::array<unsigned, 8> expected{b::DlcTotalTimeout, b::DlcInterbyteTimeout, b::DlcHeader,
                                           b::DlcLength,       b::DlcChecksum,         b::DlcInterbyteTimeout,
                                           b::DlcHeader,       b::DlcTrailing};
    for (std::size_t i = 0; i < faults.size(); ++i) {
        b::BridgeEndpoint endpoint;
        setup(endpoint, 0, 0, faults[i], 0, 50);
        feed(endpoint, hd::encode({b::Execute, Session, 5, Rpm}), 301);
        const auto frames = run(endpoint, 302, 250);
        CHECK(!frames.empty() && frames[0].type == b::Result && frames[0].payload[5] == expected[i]);
        CHECK(endpoint.state() == b::Faulted);
        const auto tx = endpoint.dlcTxBytes();
        status(exchange(endpoint, b::Execute, 6, Tps, 553), b::Error, b::NeedsNewExperiment);
        CHECK(endpoint.dlcTxBytes() == tx);
        if (faults[i] == b::Trailing)
            CHECK(response(frames[0]) == Bytes({0, 5, 9, 0xc3, 0x2f, 0x7e}));
        if (faults[i] == b::Checksum)
            CHECK(response(frames[0]) == Bytes({0, 5, 9, 0xc3, 0x2e}));
    }
    // A=ECT; B=TPS is indistinguishable by response length and MUST NOT transmit.
    b::BridgeEndpoint endpoint;
    setup(endpoint, 0, 0, b::Delay, 300);
    feed(endpoint, hd::encode({b::Execute, Session, 5, Ect}), 301);
    endpoint.tick(500);
    CHECK(endpoint.available() == 0);
    endpoint.tick(501);
    auto result = drain(endpoint);
    CHECK(result.size() == 1 && result[0].payload[5] == b::DlcTotalTimeout && endpoint.pendingDlcRx() == 4);
    status(exchange(endpoint, b::Abort, 6, {1}, 502), b::Ack, b::Ok);
    CHECK(endpoint.pendingDlcRx() == 4);
    status(exchange(endpoint, b::Execute, 7, Tps, 503), b::Error, b::NeedsNewExperiment);
    CHECK(endpoint.dlcTxBytes() == 16);
    auto late = run(endpoint, 504, 110);
    Bytes captured;
    unsigned sequence = 0;
    for (const auto &event : late) {
        CHECK(event.type == b::Event && event.request == 0 &&
              b::load16(event.payload.data() + 5) == ++sequence);
        captured.insert(captured.end(), event.payload.begin() + b::EventHeader, event.payload.end());
    }
    CHECK(captured == EctA && endpoint.pendingDlcRx() == 0 && endpoint.dlcTxBytes() == 16);
    status(exchange(endpoint, b::NewExperiment, 8, {1}, 700), b::Ack, b::Ok);
    CHECK(endpoint.generation() == 2 && endpoint.state() == b::ReadyForInit);
    // Abort before a response also retains actual late bytes; only NEW drops events.
    setup(endpoint, 0, 0, b::Delay, 300);
    feed(endpoint, hd::encode({b::Execute, Session, 5, Ect}), 301);
    feed(endpoint, hd::encode({b::Abort, Session, 6, {1}}), 302);
    auto aborted = drain(endpoint);
    CHECK(aborted.size() == 2 && aborted[0].payload[5] == b::Aborted);
    CHECK(endpoint.pendingDlcRx() == 4);
    status(exchange(endpoint, b::NewExperiment, 7, {1}, 303), b::Ack, b::Ok);
    CHECK(endpoint.pendingDlcRx() == 0 && run(endpoint, 304, 400).empty());
    setup(endpoint);
    feed(endpoint, hd::encode({b::Execute, Session, 5, Rpm}), 301);
    endpoint.tick(305);
    CHECK(endpoint.available() == 0 && endpoint.pendingDlcRx() == 3);
    feed(endpoint, hd::encode({b::Hello, 0x11223344, 1, {1, 1}}), 306);
    auto rebound = drain(endpoint);
    CHECK(rebound.size() == 2 && rebound[0].session == Session && rebound[0].request == 5);
    CHECK(rebound[0].payload[5] == b::Aborted && response(rebound[0]) == Bytes({0, 5}));
    CHECK(rebound[1].type == b::HelloInfo && rebound[1].session == 0x11223344);
    auto reboundLate = run(endpoint, 307, 10);
    Bytes remaining;
    for (const auto &event : reboundLate) {
        CHECK(event.type == b::Event && event.request == 0 && event.session == 0x11223344);
        remaining.insert(remaining.end(), event.payload.begin() + b::EventHeader, event.payload.end());
    }
    CHECK(remaining == Bytes({9, 0xc3, 0x2f}) && endpoint.dlcTxBytes() == 16);
}
class SlowPort : public b::DlcPort {
  public:
    bool stalled = false;
    unsigned count = 0;
    std::uint32_t last = 0;
    bool writeByte(std::uint8_t, std::uint32_t now) override {
        if (stalled || (count && now == last))
            return false;
        ++count;
        last = now;
        return true;
    }
    bool txIdle(std::uint32_t now) const override { return count >= 11 && std::uint32_t(now - last) >= 20; }
    bool readByte(std::uint8_t &, std::uint32_t, std::uint32_t &) override { return false; }
};
class DoubleReplyPort : public b::DlcPort {
  public:
    b::VirtualHondaEcu ecu;
    unsigned received = 0;
    std::uint32_t readAt = 0;
    bool writeByte(std::uint8_t byte, std::uint32_t now) override {
        const auto accepted = ecu.writeByte(byte, now);
        if (ecu.txBytes() == 16)
            readAt = now;
        return accepted;
    }
    bool txIdle(std::uint32_t now) const override { return ecu.txIdle(now); }
    bool readByte(std::uint8_t &byte, std::uint32_t now, std::uint32_t &at) override {
        if (ecu.readByte(byte, now, at)) {
            ++received;
            return true;
        }
        if (received < 5 || received >= 10)
            return false;
        const auto due = std::uint32_t(20 + (received - 5) * 2);
        if (std::uint32_t(now - readAt) < due)
            return false;
        byte = RpmA[received++ - 5];
        at = readAt + due;
        return true;
    }
};
void boundariesWrapAndPartialTx() {
    b::BridgeEndpoint endpoint;
    constexpr auto base = std::uint32_t(0xffffff00u);
    setup(endpoint, base);
    feed(endpoint, hd::encode({b::Execute, Session, 5, Rpm}), base + 301);
    auto frames = run(endpoint, base + 302, 199);
    CHECK(frames.size() == 1 && frames[0].payload[5] == b::Ok);
    setup(endpoint, 0, 0, b::Delay, 198); // first byte is exactly at 200 ms: too late
    feed(endpoint, hd::encode({b::Execute, Session, 5, Ect}), 301);
    frames = run(endpoint, 302, 205);
    CHECK(!frames.empty() && frames[0].payload[5] == b::DlcTotalTimeout && response(frames[0]).empty());
    setup(endpoint, 0, 0, b::Gap, 0, 60);
    feed(endpoint, hd::encode({b::Execute, Session, 5, Ect}), 301);
    endpoint.tick(401); // Entire bad-gap response available in one host poll.
    frames = drain(endpoint);
    CHECK(!frames.empty() && frames[0].payload[5] == b::DlcInterbyteTimeout);
    CHECK(b::load16(frames[0].payload.data() + 15) == 62);
    SlowPort port;
    b::TransactionEngine engine(port);
    engine.newExperiment();
    CHECK(engine.initialize(base));
    for (unsigned i = 0; i < 330; ++i)
        engine.tick(base + i);
    b::DlcResult result{};
    CHECK(!engine.takeResult(result));
    engine.tick(base + 330);
    CHECK(engine.takeResult(result));
    CHECK(result.status == b::Ok && result.txLength == 11 && result.txElapsed == 30 &&
          result.responseElapsed == 300);
    SlowPort blocked;
    blocked.stalled = true;
    b::TransactionEngine blockedEngine(blocked);
    blockedEngine.newExperiment();
    CHECK(blockedEngine.initialize(0));
    blockedEngine.tick(99);
    CHECK(!blockedEngine.takeResult(result));
    blockedEngine.tick(100);
    CHECK(blockedEngine.takeResult(result));
    CHECK(result.status == b::DlcTxTimeout && result.txLength == 0 && blocked.count == 0);
    DoubleReplyPort twice;
    b::TransactionEngine doubleEngine(twice);
    doubleEngine.newExperiment();
    CHECK(doubleEngine.initialize(0));
    doubleEngine.tick(0);
    doubleEngine.tick(300);
    CHECK(doubleEngine.takeResult(result) && result.status == b::Ok);
    CHECK(doubleEngine.execute(Rpm.data() + 2, 5, 5, 301));
    doubleEngine.tick(301);
    doubleEngine.tick(350); // both replies coalesced; never publish the first as success
    CHECK(doubleEngine.takeResult(result) && result.status == b::DlcTrailing);
    CHECK(result.rxLength == 6 && result.rx[5] == 0);
    std::array<std::uint8_t, 16> tail{};
    const auto count = doubleEngine.takeLate(tail.data(), tail.size());
    CHECK(count == 4 && Bytes(tail.begin(), tail.begin() + 4) == Bytes(RpmA.begin() + 1, RpmA.end()));
}
void stressAndOverflow() {
    b::BridgeEndpoint endpoint;
    setup(endpoint);
    std::uint32_t now = 301;
    for (std::uint32_t id = 5; id < 3005; ++id) {
        feed(endpoint, hd::encode({b::Execute, Session, id, Rpm}), now);
        auto frames = run(endpoint, now + 1, 199);
        CHECK(frames.size() == 1 && frames[0].payload[5] == b::Ok && response(frames[0]) == RpmA);
        CHECK(endpoint.buffered() == 0 && endpoint.available() == 0 && endpoint.pendingDlcRx() == 0);
        now += 201;
    }
    CHECK(endpoint.dlcTxBytes() == 15011 && endpoint.counters().txOverflows == 0);
    for (std::uint32_t id = 3005; id < 3030; ++id)
        feed(endpoint, hd::encode({b::Diagnostics, Session, id, {1}}), now);
    CHECK(endpoint.counters().txOverflows > 0 && endpoint.counters().lostEvents > 0 &&
          endpoint.state() == b::Faulted);
    auto frames = drain(endpoint);
    bool visible = false;
    for (const auto &frame : frames)
        if (frame.type == b::Error && frame.payload[6] == b::Overflow)
            visible = true;
    CHECK(visible);
    status(exchange(endpoint, b::Execute, 3030, Rpm, now), b::Error, b::NeedsNewExperiment);
    for (unsigned i = 0; i < 200; ++i) {
        status(exchange(endpoint, b::NewExperiment, 3031 + i * 2, {1}, now), b::Ack, b::Ok);
        status(exchange(endpoint, b::Abort, 3032 + i * 2, {1}, now), b::Ack, b::Ok);
    }
}
} // namespace
int main() {
    try {
        goldensAndIdentity();
        fixturesAndDuplicates();
        readOnlyPolicy();
        faultsAndLateBytes();
        boundariesWrapAndPartialTx();
        stressAndOverflow();
        std::cout << "Embedded bridge goldens, raw fixtures, whitelist, deadlines, late RX, wrap, bounded "
                     "stress PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
