#include "acceptance/runner.hpp"
#include "bridge/native_transport.hpp"
#include "support/two_nano_fixture.hpp"
#include "transport/in_memory_transport.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace hd;
std::size_t assertions{};
std::filesystem::path output = std::filesystem::current_path() / "bench-acceptance-reports";
void check(bool value, const std::string &detail) {
    ++assertions;
    if (!value)
        throw std::runtime_error(detail);
}
std::string contents(const std::filesystem::path &path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}
class MutatedUsb final : public Transport {
  public:
    MutatedUsb(Transport &transport, bool badCrc = false) : transport_(transport), badCrc_(badCrc) {}
    void start(TransportCallbacks callbacks, Time now) override {
        callbacks_ = std::move(callbacks);
        auto proxy = callbacks_;
        proxy.raw = [this](const auto &kind, auto bytes, Time at) {
            if (kind == "TX" && callbacks_.raw)
                callbacks_.raw(kind, bytes, at);
        };
        proxy.received = [this](auto bytes, Time at) {
            for (auto value : parser_.feed(bytes)) {
                if (value.type == hd_bridge::HelloInfo && !badCrc_)
                    value.payload.back() ^= 1;
                auto encoded = hd::encode(value);
                if (value.type == hd_bridge::HelloInfo && badCrc_)
                    encoded.back() ^= 1;
                if (callbacks_.raw)
                    callbacks_.raw("RX", encoded, at);
                if (callbacks_.received)
                    callbacks_.received(encoded, at);
            }
        };
        transport_.start(std::move(proxy), now);
    }
    void close() override { transport_.close(); }
    bool send(std::span<const std::uint8_t> bytes, Time now) override { return transport_.send(bytes, now); }
    void tick(Time now) override { transport_.tick(now); }
    bool isOpen() const override { return transport_.isOpen(); }
    std::size_t pendingBytes() const override { return transport_.pendingBytes() + parser_.buffered(); }
    std::string name() const override {
        return "test USB identity/CRC mutation over production bit-line fixture";
    }

  private:
    Transport &transport_;
    bool badCrc_;
    TransportCallbacks callbacks_;
    Parser parser_;
};
acceptance::Config config(const char *name) {
    acceptance::Config c;
    c.backend = acceptance::Backend::TwoNanoBench;
    c.port = "owned-bit-model-bridge";
    c.responderPort = "owned-bit-model-responder";
    c.reportDirectory = output / name;
    c.os = "controlled host time; software wired-AND bit-line model; no physical hardware";
#ifdef HD_DESKTOP_VERSION
    c.desktopVersion = HD_DESKTOP_VERSION;
#endif
#ifdef HD_DESKTOP_SHA
    c.desktopSha = HD_DESKTOP_SHA;
#endif
    return c;
}
void advance(acceptance::Runner &runner, Time end = 46000) {
    runner.start(0);
    for (Time now = 0; now <= end && !runner.done(); now += 2) {
        runner.tick(now);
        if (now % 20 == 0)
            runner.flushRecording(std::chrono::seconds(5));
    }
    check(runner.done(), "Runner bounded completion");
}
void success() {
    hd_test::TwoNanoFixture fixture;
    hd_test::TwoNanoFixture::Usb bridge(fixture, false), peer(fixture, true);
    acceptance::Runner runner(bridge, peer, config("bit-line-success"));
    advance(runner);
    check(runner.exitCode() == acceptance::ExitCode::Success, runner.message());
    check(fixture.bridge.dlcTxBytes() > 0 && fixture.responder.counters().replies > 0,
          "Both MCU endpoints used actual bit-driver TX");
    check(fixture.line.first.counters().rxBytes > 0 && fixture.line.second.counters().rxBytes > 0,
          "Both bit-driver receivers observed bytes");
    check(!bridge.isOpen() && !peer.isOpen(), "Both explicit USB paths closed");
    check(fixture.responder.quiescent() && !fixture.responder.armed(),
          "Final acknowledged quiescence before close");
    const auto report = contents(runner.reportPath());
    for (const auto *text :
         {"\"result\":\"PASS\"", "\"backend\":\"two-nano-bench\"", "\"bench_io_enabled\":true",
          "\"vehicle_connection_allowed\":false", "\"hardware_verified\":false", "\"expected_dlc_faults\":1",
          "\"observation_window_ms\":10000", "quiesce_and_close_both_devices"})
        check(report.find(text) != std::string::npos, std::string("report contains ") + text);
    const auto raw = contents(runner.journalDirectory() / "raw.jsonl");
    for (const auto *text :
         {"responder_usb_rx", "responder_usb_tx", "bridge_dlc_rx", "bridge_dlc_tx", "bench_boundary",
          "hondadash-dlc-responder-bench-v1", "\"bench_io_enabled\":true"})
        check(raw.find(text) != std::string::npos, std::string("journal contains ") + text);
    check(raw.find("\"physical_dlc_enabled\"") == std::string::npos,
          "Bench metadata never claims GPIO disabled");
    check(fixture.bridge.generation() == 2, "Exactly initial and recovery bridge NEW boundaries");
    check(fixture.responder.generation() >= 3,
          "External peer independently quiesced at start, recovery, close");
}
void strictIdentity() {
    {
        hd_test::TwoNanoFixture fixture;
        hd_test::TwoNanoFixture::Usb wrongBridge(fixture, true), wrongPeer(fixture, false);
        acceptance::Runner runner(wrongBridge, wrongPeer, config("swapped-ports"));
        advance(runner);
        check(runner.exitCode() != acceptance::ExitCode::Success, "Swapped explicit ports rejected");
        check(fixture.bridge.dlcTxBytes() == 0 && fixture.responder.counters().replies == 0,
              "Swapped identity failure causes no line TX");
        check(wrongBridge.commands.size() == 1 && wrongPeer.commands.size() <= 1,
              "Swapped endpoints receive HELLO only");
    }
    {
        bridge::NativeTransport oldBridge, oldPeer;
        acceptance::Runner runner(oldBridge, oldPeer, config("old-virtual-endpoints"));
        advance(runner);
        check(runner.exitCode() != acceptance::ExitCode::Success, "Old M2b endpoints rejected in bench mode");
        check(oldBridge.endpoint().dlcTxBytes() == 0 && oldPeer.endpoint().dlcTxBytes() == 0,
              "No virtual fallback or DLC operation");
    }
    {
        InMemoryTransport m1;
        hd_test::TwoNanoFixture fixture;
        hd_test::TwoNanoFixture::Usb peer(fixture, true);
        acceptance::Runner runner(m1, peer, config("M1-bridge-endpoint"));
        advance(runner);
        check(runner.exitCode() != acceptance::ExitCode::Success, "M1 cannot impersonate bench bridge");
        check(fixture.responderPort.txBytes() == 0, "M1 rejection never arms responder or transmits in line");
    }
    for (bool wrongBridge : {false, true})
        for (bool crc : {false, true}) {
            hd_test::TwoNanoFixture fixture;
            hd_test::TwoNanoFixture::Usb bridge(fixture, false), peer(fixture, true);
            MutatedUsb mutated(
                wrongBridge ? static_cast<Transport &>(bridge) : static_cast<Transport &>(peer), crc);
            acceptance::Runner runner(
                wrongBridge ? static_cast<Transport &>(mutated) : static_cast<Transport &>(bridge),
                wrongBridge ? static_cast<Transport &>(peer) : static_cast<Transport &>(mutated),
                config(wrongBridge ? (crc ? "bridge-bad-crc" : "unknown-bridge")
                                   : (crc ? "responder-bad-crc" : "unknown-responder")));
            advance(runner);
            check(runner.exitCode() != acceptance::ExitCode::Success,
                  "Unknown identity/outer CRC rejected independently for either device");
            check(fixture.bridge.dlcTxBytes() == 0 && fixture.responderPort.txBytes() == 0,
                  "Neither endpoint transmits line bytes before both strict identities");
            check(!bridge.isOpen() && !peer.isOpen(), "Rejected identity closes both selected ports");
        }
    check(!bench::distinctPorts("same", "same") && !bench::distinctPorts("", "peer"),
          "Explicit nonempty distinct ports required");
#ifdef _WIN32
    check(!bench::distinctPorts("COM7", "com7") && !bench::distinctPorts("COM7", "\\\\.\\COM7"),
          "Windows aliases rejected");
#endif
}
void cancellationAndArguments() {
    {
        hd_test::TwoNanoFixture fixture;
        hd_test::TwoNanoFixture::Usb bridge(fixture, false), peer(fixture, true);
        auto c = config("duplicate-ports");
        c.responderPort = c.port;
        acceptance::Runner runner(bridge, peer, c);
        runner.start(0);
        check(runner.done() && runner.exitCode() == acceptance::ExitCode::Arguments,
              "Duplicate ports rejected before opening either transport");
        check(bridge.commands.empty() && peer.commands.empty(),
              "Argument rejection sends no HELLO or lab operation");
    }
    {
        hd_test::TwoNanoFixture fixture;
        hd_test::TwoNanoFixture::Usb bridge(fixture, false), peer(fixture, true);
        acceptance::Runner runner(bridge, peer, config("cancelled"));
        runner.start(0);
        Time now = 0;
        for (; now < 4000; now += 2) {
            runner.tick(now);
            if (now % 20 == 0)
                runner.flushRecording(std::chrono::seconds(5));
        }
        runner.cancel(now);
        for (; now < 7000 && !runner.done(); now += 2) {
            runner.tick(now);
            if (now % 20 == 0)
                runner.flushRecording(std::chrono::seconds(5));
        }
        check(runner.done() && runner.exitCode() == acceptance::ExitCode::Cancelled,
              "Cancellation awaits bounded asynchronous closure");
        check(fixture.responder.quiescent() && !bridge.isOpen() && !peer.isOpen(),
              "Cancel drains and acknowledges peer TX stopped before closing");
        check(contents(runner.journalDirectory() / "raw.jsonl").find("responder_diagnostics") !=
                  std::string::npos,
              "Final MCU diagnostics included in journal");
    }
}
void absentPeer() {
    hd_test::TwoNanoFixture fixture;
    hd_test::TwoNanoFixture::Usb bridge(fixture, false), peer(fixture, true);
    bench::Controller controller(bridge, peer);
    dlc::Session session(controller, dlc::bridgePollingSettings(), dlc::bridgeFreshness());
    controller.connect(0);
    Time now = 0;
    for (; now < 100; ++now)
        session.tick(now);
    check(controller.state() == bench::State::Ready && fixture.bridge.dlcTxBytes() == 0,
          "Two handshakes perform no line TX");
    session.start(now);
    for (; now < 300; ++now)
        session.tick(now);
    fixture.responderPowered = false;
    for (; now < 1200; ++now)
        session.tick(now);
    check(session.state() == dlc::State::Faulted,
          "Missing external responder faults, never virtual fallback");
    check(!session.model().current(Channel::Rpm, now), "Absent peer never fabricates RPM");
    const auto count = fixture.bridge.dlcTxBytes();
    for (; now < 1800; ++now)
        session.tick(now);
    check(fixture.bridge.dlcTxBytes() == count, "No subsequent line TX after DLC timeout");
    controller.disconnect(now);
}
struct Manual {
    hd_test::TwoNanoFixture fixture;
    hd_test::TwoNanoFixture::Usb bridge{fixture, false}, peer{fixture, true};
    bench::Controller controller{bridge, peer};
    dlc::Session session{controller, dlc::bridgePollingSettings(), dlc::bridgeFreshness()};
    Time now{};
    std::uint64_t unassociated{};
    Manual() {
        session.onRaw = [this](const RawEvent &event) {
            if (event.kind == "bridge_event" && !event.bytes.empty())
                unassociated += event.bytes.size();
        };
        controller.connect(now);
        wait([&] { return controller.state() == bench::State::Ready; }, 200, "both handshakes");
        session.start(now);
        wait([&] { return session.model().current(Channel::Coolant, now).has_value(); }, 2500,
             "first complete A");
    }
    template <class Predicate> void wait(Predicate predicate, Time budget, const char *detail) {
        const auto end = now + budget;
        while (now < end && !predicate())
            session.tick(++now);
        check(predicate(), std::string(detail) + ": " + controller.error() + " / " + session.error());
    }
    void forTime(Time duration) {
        const auto end = now + duration;
        while (now < end)
            session.tick(++now);
    }
    void delayedEct(Time delay) {
        wait([&] { return controller.canExecute(); }, 500, "idle before manual whitelisted ECT");
        dlc::Faults fault;
        fault.delayMs = delay;
        session.setFaults(fault);
        do {
            controller.tick(++now);
        } while (!controller.canExecute() && now < 10000);
        check(controller.canExecute(), "CONFIG ACK before controlled late ECT");
        check(session.request(dlc::Operation::Read, {0x10, 1}, now),
              "Explicit ECT accepted through Session whitelist");
    }
    void recover() {
        session.setFaults({});
        session.setScenario(dlc::Scenario::Baseline);
        session.start(now);
        wait([&] { return session.model().current(Channel::Coolant, now) == 61; }, 3000,
             "coordinated explicit recovery");
    }
};
void lateAndReset() {
    {
        Manual h;
        const auto tpsBefore = h.session.model().channels()[channelIndex(Channel::Throttle)].lastValid;
        h.delayedEct(300);
        h.wait([&] { return h.session.state() == dlc::State::Faulted; }, 1000,
               "late ECT faults at original deadline");
        const auto tx = h.fixture.bridge.dlcTxBytes();
        check(!h.session.request(dlc::Operation::Read, {0x14, 1}, h.now), "Faulted ECT cannot start TPS");
        h.forTime(600);
        check(h.unassociated >= 4, "Late external ECT retained in raw trace after bridge ABORT");
        check(h.session.model().channels()[channelIndex(Channel::Throttle)].lastValid == tpsBefore,
              "Late ECT never refreshes TPS");
        check(h.fixture.bridge.dlcTxBytes() == tx, "No polling after late ECT error");
        h.recover();
        check(h.fixture.responder.generation() >= 2,
              "Recovery explicitly advanced external responder generation");
        h.controller.disconnect(h.now);
    }
    for (bool resetBridge : {false, true}) {
        Manual h;
        h.delayedEct(300);
        h.forTime(20);
        if (resetBridge)
            h.fixture.bridge.reset(h.fixture.line.millis());
        else
            h.fixture.responder.reset(h.fixture.line.millis());
        h.wait([&] { return h.session.state() == dlc::State::Faulted; }, 2000,
               "board reset requires explicit state verification");
        const auto tx = h.fixture.bridge.dlcTxBytes();
        h.forTime(500);
        check(h.fixture.bridge.dlcTxBytes() == tx, "Reset does not automatically retry EXECUTE");
        h.controller.connect(h.now);
        h.wait([&] { return h.controller.state() == bench::State::Ready; }, 200,
               "repeat both strict identities after reset");
        h.recover();
        check(h.session.model().current(Channel::Throttle, h.now) == 32,
              "Reset recovery restores values through signal path");
        h.controller.disconnect(h.now);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--report-dir")
            output = std::filesystem::absolute(argv[2]);
        else if (argc != 1)
            throw std::runtime_error("expected --report-dir <directory>");
        success();
        strictIdentity();
        cancellationAndArguments();
        absentPeer();
        lateAndReset();
        std::cout << assertions << " bench integration assertions passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "bench integration: " << e.what() << '\n';
        return 1;
    }
}
