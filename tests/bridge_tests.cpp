#include "bridge/client.hpp"
#include "bridge/native_transport.hpp"
#include "honda_dlc/session.hpp"
#include "recording/recorder.hpp"
#include "transport/in_memory_transport.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace hd;
namespace b = hd::bridge;
namespace d = hd::dlc;
namespace w = hd_bridge;
void require(bool ok, const char *what, int line) {
    if (!ok)
        throw std::runtime_error(std::to_string(line) + ": " + what);
}
#define CHECK(...) require(bool((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
void advance(d::Session &session, Time from, Time to, Time step = 1) {
    for (Time t = from; t <= to; t += step)
        session.tick(t);
}
constexpr d::Settings Slow{200, 50, 10000, 10000};

// Fault adapter modifies USB delivery only. The actual production embedded bridge
// and VirtualHondaEcu still perform every read and produce each original result.
class UsbFaultAdapter : public Transport {
  public:
    b::NativeTransport inner;
    std::function<void(Frame &)> modify;
    bool corruptResult{}, dropResult{}, duplicateResult{};
    Time splitResultDelayMs{};
    std::optional<Frame> saved;
    std::vector<TransportCallbacks> retired;
    void start(TransportCallbacks callbacks, Time now) override {
        if (callbacks_.received)
            retired.push_back(callbacks_);
        callbacks_ = std::move(callbacks);
        parser_.reset();
        TransportCallbacks c;
        c.opened = [this](Time t) {
            if (callbacks_.opened)
                callbacks_.opened(t);
        };
        c.sent = [this](Time t) {
            if (callbacks_.sent)
                callbacks_.sent(t);
        };
        c.error = [this](auto reason, Time t) {
            if (callbacks_.error)
                callbacks_.error(reason, t);
        };
        c.raw = [this](const auto &kind, auto bytes, Time t) {
            if (kind != "RX" && callbacks_.raw)
                callbacks_.raw(kind, bytes, t);
        };
        c.received = [this](auto bytes, Time t) {
            for (auto frame : parser_.feed(bytes)) {
                if (frame.type == w::Result && frame.payload[6] == w::Execute) {
                    saved = frame;
                    if (dropResult)
                        continue;
                }
                if (modify)
                    modify(frame);
                auto output = hd::encode(frame);
                if (corruptResult && frame.type == w::Result && frame.payload[6] == w::Execute)
                    output.back() ^= 1;
                if (splitResultDelayMs && frame.type == w::Result && frame.payload[6] == w::Execute) {
                    const auto half = output.size() / 2;
                    deliver(std::span(output).first(half), t);
                    delayed_.push_back({t + splitResultDelayMs, {output.begin() + half, output.end()}});
                    continue;
                }
                deliver(output, t);
                if (duplicateResult && frame.type == w::Result && frame.payload[6] == w::Execute)
                    deliver(output, t);
            }
        };
        inner.start(std::move(c), now);
    }
    void deliver(std::span<const std::uint8_t> bytes, Time now) {
        if (callbacks_.raw)
            callbacks_.raw("RX", bytes, now);
        if (callbacks_.received)
            callbacks_.received(bytes, now);
    }
    void close() override { inner.close(); }
    bool send(std::span<const std::uint8_t> bytes, Time now) override { return inner.send(bytes, now); }
    void tick(Time now) override {
        inner.tick(now);
        while (!delayed_.empty() && delayed_.front().first <= now) {
            auto bytes = std::move(delayed_.front().second);
            delayed_.pop_front();
            deliver(bytes, now);
        }
    }
    bool isOpen() const override { return inner.isOpen(); }
    std::size_t pendingBytes() const override { return inner.pendingBytes(); }
    std::string name() const override { return "test USB fault adapter"; }

  private:
    TransportCallbacks callbacks_;
    Parser parser_;
    std::deque<std::pair<Time, std::vector<std::uint8_t>>> delayed_;
};

void sharedDecoderAndRecording() {
    b::NativeTransport native;
    b::Client client(native);
    d::Session bridge(client, Slow), direct(Slow);
    std::vector<Sample> actual, expected;
    std::vector<RawEvent> raw;
    bridge.onSample = [&](const Sample &s) { actual.push_back(s); };
    direct.onSample = [&](const Sample &s) { expected.push_back(s); };
    bridge.onRaw = [&](const RawEvent &e) { raw.push_back(e); };
    client.connect(0);
    advance(bridge, 0, 10);
    CHECK(client.state() == b::State::Ready && client.info());
    CHECK(native.endpoint().dlcTxBytes() == 0); // Handshake cannot initiate DLC.
    CHECK(client.info()->identity == w::Identity && client.info()->backend == 1);
    CHECK(!client.info()->physicalDlcEnabled);
    bridge.start(11);
    direct.start(11);
    advance(bridge, 12, 1100);
    advance(direct, 12, 1100);
    CHECK(bridge.state() == d::State::Polling && bridge.stats().accepted == 3);
    CHECK(direct.stats().accepted == 3 && actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        CHECK(actual[i].values == expected[i].values);
        CHECK(actual[i].qualities == expected[i].qualities);
        CHECK(actual[i].reasons == expected[i].reasons);
        CHECK(actual[i].updatedMask == expected[i].updatedMask);
        CHECK(actual[i].source == expected[i].source);
        if (i)
            CHECK(actual[i].freshnessSince && *actual[i].freshnessSince < actual[i].time);
    }
    CHECK(bridge.model().current(Channel::Rpm, 1100) == 750);
    CHECK(bridge.model().current(Channel::Coolant, 1100) == 61);
    CHECK(bridge.model().current(Channel::Throttle, 1100) == 32);
    const auto ectStamp = bridge.model().channels()[2].lastValid;
    CHECK(bridge.request(d::Operation::Read, {0, 2}, 1101));
    advance(bridge, 1102, 1320);
    CHECK(bridge.model().channels()[2].lastValid == ectStamp);
    CHECK(bridge.model().channels()[0].lastValid != ectStamp);
    const auto before = native.endpoint().dlcTxBytes();
    CHECK(!bridge.request(d::Operation::Write, {0, 2}, 1321));
    CHECK(!bridge.request(d::Operation::Read, {0x10, 2}, 1321));
    CHECK(native.endpoint().dlcTxBytes() == before);
    bridge.setScenario(d::Scenario::Higher);
    advance(bridge, 1322, 1330); // CONFIG completes independently; no data fabricated.
    CHECK(bridge.model().current(Channel::Rpm, 1330) == 750);
    CHECK(bridge.request(d::Operation::Read, {0, 2}, 1331));
    advance(bridge, 1332, 1550);
    CHECK(bridge.model().current(Channel::Rpm, 1550) == 1500);
    CHECK(bridge.request(d::Operation::Read, {0x10, 1}, 1551));
    advance(bridge, 1552, 1770);
    CHECK(bridge.model().current(Channel::Coolant, 1770) == 89);
    CHECK(bridge.request(d::Operation::Read, {0x14, 1}, 1771));
    advance(bridge, 1772, 1990);
    CHECK(bridge.model().current(Channel::Throttle, 1990) == 75);
    const auto countKind = [&](const char *kind) {
        return std::count_if(raw.begin(), raw.end(), [=](const auto &e) { return e.kind == kind; });
    };
    CHECK(countKind("usb_tx") > 0 && countKind("usb_rx") > 0);
    CHECK(countKind("bridge_dlc_tx") > 0 && countKind("bridge_dlc_rx") > 0);
    CHECK(countKind("bridge_boundary") == 1);
    CHECK(std::any_of(raw.begin(), raw.end(), [](const auto &e) {
        return e.kind == "bridge_dlc_rx" && e.bytes == std::vector<std::uint8_t>{0, 5, 9, 0xc3, 0x2f} &&
               e.bridge && e.bridge->rxElapsedMs == 200;
    }));

    const auto directory =
        std::filesystem::temp_directory_path() / std::filesystem::path(u8"HondaDash-M2b-тест");
    Recorder recorder;
    RecordingMetadata meta;
    meta.formatVersion = 3;
    meta.wireProtocol = "honda-dlc";
    meta.profile = d::ProfileId;
    meta.transport = "native-bridge-lab";
    meta.endpoint = w::Identity;
    meta.bridgeIdentity = w::Identity;
    meta.bridgeVersion = 1;
    meta.backend = "virtual";
    meta.readPolicyVersion = 1;
    meta.outerProtocol = "hondadash-bridge-v1";
    CHECK(recorder.start(directory, meta));
    bridge.onSample = [&](const Sample &s) { CHECK(recorder.enqueueSample(s)); };
    bridge.onRaw = [&](const RawEvent &e) { CHECK(recorder.enqueueRaw(e)); };
    bridge.start(2000);
    advance(bridge, 2001, 2940);
    bridge.stop(2941);
    advance(bridge, 2942, 2950);
    recorder.stop();
    CHECK(recorder.error().empty());
    std::ifstream file(recorder.directory() / "raw.jsonl", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    CHECK(text.find("\"backend\":\"virtual\"") != std::string::npos);
    CHECK(text.find("bridge_dlc_rx") != std::string::npos);
    CHECK(text.find("usb_rx") != std::string::npos);
    CHECK(text.find("freshness_lower_bound_ms") != std::string::npos);
    CHECK(text.find("\"bridge\":{") != std::string::npos);
    CHECK(text.find("physical_dlc_enabled\":false") != std::string::npos);
}

void timingAndAmbiguity() {
    // Independent device epoch crosses uint32 wrap during the actual embedded init.
    b::NativeTransport native({0xfffffff0u, 0, 1});
    b::Client client(native);
    d::Session session(client, Slow, {100, 3000});
    session.start(0);
    advance(session, 0, 1000);
    CHECK(session.stats().accepted == 3 && client.state() == b::State::Running);
    native.setUsbDelay(300); // DLC is timely; USB delivery of its result is delayed.
    CHECK(session.request(d::Operation::Read, {0, 2}, 1001));
    advance(session, 1002, 1530);
    CHECK(session.stats().accepted == 4);
    CHECK(session.model().channels()[0].lastValid == 1001); // Not newly fresh at result arrival.
    CHECK(session.model().channels()[0].quality == Quality::Stale);
    CHECK(client.diagnostics().rxElapsedMs == 200 && client.diagnostics().maxGapMs == 2);
    native.setUsbDelay(0);
    session.setFaults({false, 300});
    advance(session, 1531, 1540);
    const auto ectStamp = session.model().channels()[2].lastValid;
    const auto tpsStamp = session.model().channels()[4].lastValid;
    std::vector<RawEvent> raw;
    session.onRaw = [&](const RawEvent &e) { raw.push_back(e); };
    CHECK(session.request(d::Operation::Read, {0x10, 1}, 1541));
    advance(session, 1542, 1750);
    CHECK(session.state() == d::State::Faulted && client.state() == b::State::Faulted);
    CHECK(client.diagnostics().status == w::DlcTotalTimeout);
    CHECK(native.endpoint().pendingDlcRx() == 4);
    const auto tx = native.endpoint().dlcTxBytes();
    CHECK(!session.request(d::Operation::Read, {0x14, 1}, 1751));
    advance(session, 1752, 1900);
    CHECK(native.endpoint().dlcTxBytes() == tx);
    CHECK(native.endpoint().pendingDlcRx() == 0);
    CHECK(session.model().channels()[2].lastValid == ectStamp);
    CHECK(session.model().channels()[4].lastValid == tpsStamp);
    std::vector<std::uint8_t> late;
    for (const auto &e : raw)
        if (e.kind == "bridge_event") {
            CHECK(e.request == 0);
            late.insert(late.end(), e.bytes.begin(), e.bytes.end());
        }
    CHECK(late == std::vector<std::uint8_t>({0, 4, 0x40, 0xbc}));
    session.setFaults({});
    session.start(1901);
    advance(session, 1902, 2850);
    CHECK(session.stats().accepted == 3 && session.state() == d::State::Polling);
    CHECK(client.info()->generation == 2);

    // Host watchdog and age budget are separate limits. A late-but-delivered
    // success cannot turn into a fresh number merely because USB is reliable.
    native.setUsbDelay(1100);
    CHECK(session.request(d::Operation::Read, {0, 2}, 2851));
    advance(session, 2852, 4200);
    CHECK(session.state() == d::State::Faulted);
    CHECK(client.error().find("age budget") != std::string::npos);
}

void faultsCancelAndReset() {
    std::array<d::Faults, 7> cases{};
    cases[0].corruptNext = true;
    cases[1].wrongLengthNext = true;
    cases[2].truncateNext = true;
    cases[3].noiseNext = true;
    cases[4].trailingNext = true;
    cases[5].gapMs = 50;
    cases[6].headerNext = true;
    for (const auto fault : cases) {
        b::NativeTransport native;
        b::Client client(native);
        d::Session session(client, Slow);
        session.setFaults(fault);
        session.start(0);
        advance(session, 0, 1000);
        CHECK(session.stats().accepted == 0 && session.state() == d::State::Faulted);
        CHECK(client.state() == b::State::Faulted);
        const auto tx = native.endpoint().dlcTxBytes();
        advance(session, 1001, 2000);
        CHECK(native.endpoint().dlcTxBytes() == tx);
        session.setFaults({});
        session.start(2001);
        advance(session, 2002, 3000);
        CHECK(session.stats().accepted == 3);
    }
    b::NativeTransport native;
    b::Client client(native);
    d::Session session(client, Slow);
    session.start(0);
    advance(session, 0, 1000);
    session.setFaults({false, 300});
    advance(session, 1001, 1010);
    CHECK(session.request(d::Operation::Read, {0x10, 1}, 1011));
    advance(session, 1012, 1020);
    CHECK(native.endpoint().pendingDlcRx() == 4);
    std::size_t late = 0;
    session.onRaw = [&](const RawEvent &e) {
        if (e.kind == "bridge_event")
            late += e.bytes.size();
    };
    session.stop(1021);
    advance(session, 1022, 1400);
    CHECK(late == 4 && native.endpoint().pendingDlcRx() == 0);
    CHECK(session.model().channels()[2].quality == Quality::NoData);
    session.setFaults({});
    session.start(1401);
    advance(session, 1402, 2400);
    CHECK(session.stats().accepted == 3);
    native.resetDevice(2401);
    CHECK(session.request(d::Operation::Read, {0, 2}, 2402));
    advance(session, 2403, 2500);
    CHECK(session.state() == d::State::Faulted && !client.info());
    const auto accepted = session.stats().accepted;
    advance(session, 2501, 3500);
    CHECK(session.stats().accepted == accepted); // No silent resume after reset.
    session.start(3501);
    advance(session, 3502, 4500);
    CHECK(session.stats().accepted == 3 && client.info());
}

void usbFaultsAndIdentity() {
    for (unsigned field : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 15u}) {
        UsbFaultAdapter transport;
        transport.modify = [=](Frame &f) {
            if (f.type == w::HelloInfo)
                f.payload[field] ^= 1;
        };
        b::Client client(transport);
        d::Session session(client);
        session.start(0);
        advance(session, 0, 50);
        CHECK(client.state() == b::State::Faulted && !client.info());
        CHECK(transport.inner.endpoint().dlcTxBytes() == 0);
    }
    InMemoryTransport m1;
    b::Client wrong(m1);
    d::Session rejected(wrong);
    rejected.start(0);
    advance(rejected, 0, 3000);
    CHECK(rejected.state() == d::State::Faulted && !wrong.info());
    b::NativeTransport newFirmware;
    hd::Session oldClient(newFirmware);
    oldClient.start(0);
    for (Time t = 0; t < 3000; ++t)
        oldClient.tick(t);
    CHECK(oldClient.state() == hd::SessionState::Faulted);
    CHECK(newFirmware.endpoint().dlcTxBytes() == 0);

    for (unsigned mode = 0; mode < 3; ++mode) {
        UsbFaultAdapter transport;
        transport.corruptResult = mode == 0;
        transport.dropResult = mode == 1;
        if (mode == 2)
            transport.modify = [](Frame &f) {
                if (f.type == w::Result && f.payload[6] == w::Execute)
                    f.payload.back() ^= 1;
            };
        b::Client client(transport);
        d::Session session(client);
        session.start(0);
        advance(session, 0, 2300);
        CHECK(session.state() == d::State::Faulted && session.stats().accepted == 0);
        CHECK(transport.inner.endpoint().dlcTxBytes() == 16); // 11 init + one read, no retry.
    }
    UsbFaultAdapter transport;
    transport.duplicateResult = true;
    b::Client client(transport);
    d::Session session(client, Slow);
    session.start(0);
    advance(session, 0, 1000);
    CHECK(session.stats().accepted == 3); // duplicates cannot refresh data
    const auto old = transport.saved;
    session.stop(1001);
    client.disconnect(1001);
    session.start(1002);
    advance(session, 1003, 2000);
    CHECK(session.stats().accepted == 3 && !transport.retired.empty());
    const auto oldBytes = hd::encode(*old);
    transport.deliver(oldBytes, 2001);
    transport.retired.front().received(oldBytes, 2002);
    transport.retired.front().error("stale callback", 2002);
    CHECK(session.stats().accepted == 3 && client.state() == b::State::Running);
}

void controlBoundariesAndHostWatchdogs() {
    class StalledTransport : public Transport {
      public:
        bool opens{}, partial{};
        std::size_t transmitted{};
        void start(TransportCallbacks c, Time now) override {
            callbacks = std::move(c);
            if (opens && callbacks.opened)
                callbacks.opened(now);
        }
        bool send(std::span<const std::uint8_t> bytes, Time now) override {
            if (partial && callbacks.raw) {
                transmitted += 3;
                callbacks.raw("TX", bytes.first(3), now);
            }
            return true; // Explicitly never reports a completed local TX.
        }
        void close() override {}
        void tick(Time) override {}
        bool isOpen() const override { return opens; }
        std::size_t pendingBytes() const override { return partial ? 3 : 0; }
        std::string name() const override { return "stalled test transport"; }
        TransportCallbacks callbacks;
    };
    for (unsigned mode = 0; mode < 3; ++mode) {
        StalledTransport transport;
        transport.opens = mode != 0;
        transport.partial = mode == 2;
        b::Client client(transport);
        d::Session session(client);
        session.start(0);
        advance(session, 0, mode == 0 ? 1999 : 499);
        CHECK(session.state() == d::State::Initializing);
        session.tick(mode == 0 ? 2000 : 500);
        CHECK(session.state() == d::State::Faulted);
        CHECK(client.error().find(mode == 0 ? "open watchdog" : "transmit watchdog") != std::string::npos);
        CHECK(transport.transmitted == (mode == 2 ? 3u : 0u));
    }
    // USB inter-fragment pause exceeds the DLC interbyte deadline, yet its
    // embedded response was timely and remains within the host result budget.
    UsbFaultAdapter delayed;
    delayed.splitResultDelayMs = 100;
    b::Client client(delayed);
    d::Session session(client, Slow);
    session.start(0);
    advance(session, 0, 1350);
    CHECK(session.stats().accepted == 3 && session.state() == d::State::Polling);
    CHECK(client.diagnostics().maxGapMs == 2);
    session.stop(1351);
    client.disconnect(1351);
    client.connect(1352);
    advance(session, 1353, 1400);
    CHECK(client.state() == b::State::Ready);
    const auto tx = delayed.inner.endpoint().dlcTxBytes();
    advance(session, 1401, 1800);
    CHECK(delayed.inner.endpoint().dlcTxBytes() == tx); // reconnect is handshake-only

    session.setScenario(d::Scenario::Boundary);
    session.start(1801);
    advance(session, 1802, 3200);
    CHECK(session.stats().accepted == 3);
    CHECK(session.model().channels()[0].quality == Quality::Invalid);
    CHECK(!session.model().current(Channel::Rpm, 3200));
    CHECK(session.model().current(Channel::Coolant, 3200) == -43);
    CHECK(session.model().current(Channel::Throttle, 3200) == 0);
    CHECK(!session.model().channels()[0].reason.empty());
}

void sustainedAndBounded() {
    b::NativeTransport native;
    b::Client client(native);
    d::Session session(client);
    session.start(0);
    for (Time t = 0; t < 650000; t += 2) {
        session.tick(t);
        CHECK(native.pendingBytes() <= b::NativeTransport::Capacity);
        CHECK(native.endpoint().available() <= w::TxCapacity);
        CHECK(native.endpoint().buffered() <= w::MaxFrame);
        CHECK(session.pendingBytes() <= b::NativeTransport::Capacity + MaxFrame);
    }
    CHECK(session.stats().accepted > 3000 && client.state() == b::State::Running);
    for (Time i = 0; i < 100; ++i) {
        const auto at = 650000 + i * 20;
        session.stop(at);
        advance(session, at + 1, at + 3);
        session.start(at + 4);
        advance(session, at + 5, at + 19);
    }
    CHECK(native.pendingBytes() <= b::NativeTransport::Capacity);
}
void callbackCancellation() {
    b::NativeTransport native;
    b::Client client(native);
    d::Session session(client);
    session.onRaw = [&](const RawEvent &event) {
        if (event.kind == "bridge_boundary")
            session.stop(event.time);
    };
    session.start(0);
    advance(session, 0, 100);
    CHECK(session.state() == d::State::Stopped);
    CHECK(client.state() == b::State::Ready && native.endpoint().dlcTxBytes() == 0);
    session.onRaw = [&](const RawEvent &event) {
        if (event.kind == "tx_queued" && event.request)
            session.stop(event.time);
    };
    session.start(101);
    advance(session, 102, 1000);
    CHECK(session.state() == d::State::Stopped && session.stats().accepted == 0);
    CHECK(native.endpoint().dlcTxBytes() == 11); // cancellation before any actual read
    session.onRaw = {};
    session.onSample = [&](const Sample &sample) {
        if (sample.request)
            session.stop(sample.time);
    };
    session.start(1001);
    advance(session, 1002, 2000);
    CHECK(session.state() == d::State::Stopped && session.stats().accepted == 1);
    CHECK(session.model().channels()[0].quality == Quality::NoData);
}
} // namespace
int main() {
    try {
        sharedDecoderAndRecording();
        timingAndAmbiguity();
        faultsCancelAndReset();
        usbFaultsAndIdentity();
        controlBoundariesAndHostWatchdogs();
        sustainedAndBounded();
        callbackCancellation();
        std::cout << "Bridge host production path, independent clocks, USB/DLC faults, cancellation, "
                     "recording and bounds PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
