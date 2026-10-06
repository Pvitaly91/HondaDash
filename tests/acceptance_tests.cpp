#include "acceptance/runner.hpp"
#include "bridge/native_transport.hpp"
#include "protocol/protocol.hpp"
#include "transport/in_memory_transport.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef HD_DESKTOP_VERSION
#define HD_DESKTOP_VERSION "unknown"
#endif
#ifndef HD_DESKTOP_SHA
#define HD_DESKTOP_SHA "unknown"
#endif

namespace {
using namespace hd;
namespace a = hd::acceptance;
namespace w = hd_bridge;
std::size_t assertions{};
void check(bool condition, const std::string &detail) {
    ++assertions;
    if (!condition)
        throw std::runtime_error(detail);
}
std::filesystem::path root = std::filesystem::current_path() / "acceptance-reports";
a::Config config(std::string name) {
    a::Config value;
    value.reportDirectory = root / name;
    value.desktopVersion = HD_DESKTOP_VERSION;
    value.desktopSha = HD_DESKTOP_SHA;
#ifdef _WIN32
    value.os = "Windows controlled-time";
#else
    value.os = "Linux controlled-time";
#endif
    return value;
}
std::string contents(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void advance(a::Runner &runner, Time end = 46000) {
    runner.start(0);
    for (Time now = 0; now <= end && !runner.done(); now += 2) {
        runner.tick(now);
        if (now % 20 == 0)
            runner.flushRecording(std::chrono::seconds(5));
    }
    check(runner.done(), "runner exceeded controlled-time test bound");
}
// Negative-path fault adapter only. It keeps the production native endpoint,
// production outer parser and production Client; expected values are not injected.
class MutatedTransport final : public Transport {
  public:
    enum class Fault { WrongIdentity, OuterCrc, InnerChecksum };
    explicit MutatedTransport(Fault fault) : fault_(fault) {}
    bridge::NativeTransport native;
    std::vector<std::uint8_t> commands;
    void start(TransportCallbacks callbacks, Time now) override {
        callbacks_ = std::move(callbacks);
        parser_.reset();
        auto proxy = callbacks_;
        proxy.raw = [this](const auto &kind, auto bytes, Time at) {
            if (kind == "TX" && callbacks_.raw)
                callbacks_.raw(kind, bytes, at);
        };
        proxy.received = [this](auto bytes, Time at) {
            for (auto frame : parser_.feed(bytes)) {
                if (fault_ == Fault::WrongIdentity && frame.type == w::HelloInfo)
                    frame.payload.back() ^= 1;
                auto output = hd::encode(frame);
                if (fault_ == Fault::OuterCrc && frame.type == w::Result)
                    output.back() ^= 1;
                if (callbacks_.raw)
                    callbacks_.raw("RX", output, at);
                if (callbacks_.received)
                    callbacks_.received(output, at);
            }
        };
        native.start(std::move(proxy), now);
    }
    void close() override { native.close(); }
    bool send(std::span<const std::uint8_t> bytes, Time now) override {
        Parser parser;
        const auto frames = parser.feed(bytes);
        if (frames.empty())
            return false;
        auto frame = frames.front();
        commands.push_back(frame.type);
        if (fault_ == Fault::InnerChecksum && frame.type == w::Configure) {
            frame.payload[2] = w::Checksum;
            return native.send(hd::encode(frame), now);
        }
        return native.send(bytes, now);
    }
    void tick(Time now) override { native.tick(now); }
    bool isOpen() const override { return native.isOpen(); }
    std::size_t pendingBytes() const override { return native.pendingBytes(); }
    std::string name() const override { return "native production endpoint with test USB mutation"; }

  private:
    Fault fault_;
    TransportCallbacks callbacks_;
    Parser parser_;
};
class SilentTransport final : public Transport {
  public:
    void start(TransportCallbacks callbacks, Time now) override {
        callbacks_ = std::move(callbacks);
        open_ = true;
        callbacks_.opened(now);
    }
    bool send(std::span<const std::uint8_t> bytes, Time now) override {
        if (callbacks_.raw)
            callbacks_.raw("TX", bytes, now);
        if (callbacks_.sent)
            callbacks_.sent(now);
        return true;
    }
    void tick(Time) override {}
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    std::size_t pendingBytes() const override { return 0; }
    std::string name() const override { return "silent negative-path transport"; }

  private:
    TransportCallbacks callbacks_;
    bool open_{};
};
void success() {
    bridge::NativeTransport native({0xfffffff0u, 0, 1});
    a::Runner runner(native, config("native"));
    advance(runner);
    check(runner.exitCode() == a::ExitCode::Success, runner.message());
    check(!native.isOpen(), "success closes transport");
    check(native.endpoint().generation() == 2, "only explicit initial/recovery experiment boundaries");
    const auto report = contents(runner.reportPath());
    for (const auto *text : {"\"result\":\"PASS\"", "\"expected_dlc_faults\":1", "\"port_closed\":true",
                             "\"journal_closed\":true", "\"hardware_verified\":false",
                             "\"observation_window_ms\":10000", "fault_drain_stale_hide_no_polling",
                             "explicit_new_experiment_recovery", "\"stale_ms\":1400", "\"stale_ms\":2100"})
        check(report.find(text) != std::string::npos, std::string("report missing: ") + text);
    const auto raw = contents(runner.journalDirectory() / "raw.jsonl");
    check(raw.find("\"kind\":\"usb_rx\"") != std::string::npos &&
              raw.find("\"kind\":\"bridge_dlc_rx\"") != std::string::npos,
          "production Recorder contains both byte layers");
    check(raw.find("\"freshness_policy\":\"bridge-bounded-age-v1\"") != std::string::npos,
          "Recorder uses same explicit policy");
}
void rejection() {
    for (auto fault : {MutatedTransport::Fault::WrongIdentity, MutatedTransport::Fault::OuterCrc,
                       MutatedTransport::Fault::InnerChecksum}) {
        const auto label = fault == MutatedTransport::Fault::WrongIdentity ? "wrong-identity"
                           : fault == MutatedTransport::Fault::OuterCrc    ? "outer-crc"
                                                                           : "unexpected-inner-checksum";
        MutatedTransport transport(fault);
        a::Runner runner(transport, config(label));
        advance(runner);
        check(runner.exitCode() == a::ExitCode::Endpoint, std::string(label) + ": " + runner.message());
        check(!transport.isOpen(), "failure closes transport");
        if (fault == MutatedTransport::Fault::WrongIdentity) {
            check(transport.commands == std::vector<std::uint8_t>{w::Hello},
                  "identity failure sends HELLO only");
            check(transport.native.endpoint().dlcTxBytes() == 0, "no DLC bytes before verified identity");
        }
    }
    InMemoryTransport m1;
    a::Runner wrong(m1, config("M1-endpoint"));
    advance(wrong);
    check(wrong.exitCode() != a::ExitCode::Success, "actual M1 endpoint cannot pass");
    SilentTransport silent;
    a::Runner timeout(silent, config("timeout"));
    advance(timeout);
    check(timeout.exitCode() == a::ExitCode::Timeout, "handshake timeout remains timeout");
}
void cancellationAndIo() {
    bridge::NativeTransport native;
    const auto cancelledConfig = config("cancelled");
    std::filesystem::create_directories(cancelledConfig.reportDirectory);
    {
        std::ofstream oldReport(cancelledConfig.reportDirectory / "report.json");
        oldReport << "{\"result\":\"PASS\"}\n";
    }
    a::Runner cancelled(native, cancelledConfig);
    cancelled.start(0);
    check(contents(cancelled.reportPath()).find("\"result\":\"RUNNING\"") != std::string::npos,
          "starting a new check retires previous PASS before laboratory commands");
    check(native.endpoint().dlcTxBytes() == 0, "report preflight sends no DLC bytes");
    for (Time now = 0; now < 4000; now += 2) {
        cancelled.tick(now);
        if (now % 20 == 0)
            cancelled.flushRecording(std::chrono::seconds(5));
    }
    cancelled.cancel(4000);
    check(cancelled.done() && cancelled.exitCode() == a::ExitCode::Cancelled,
          "explicit cancellation is not PASS");
    check(!native.isOpen(), "cancellation closes transport");
    bridge::NativeTransport disk;
    a::Runner failure(disk, config("recording-error"), [] { return false; });
    advance(failure);
    check(failure.exitCode() == a::ExitCode::Recording, "disk write failure is not PASS");
    bridge::NativeTransport report;
    const auto output = config("report-error");
    std::filesystem::create_directories(output.reportDirectory / "report.json");
    a::Runner noReport(report, output);
    noReport.start(0);
    check(noReport.done(), "report preflight fails before experiment");
    noReport.cancel(1);
    check(noReport.exitCode() == a::ExitCode::Report, "report write failure overrides PASS/cancel");
    check(!report.isOpen(), "report failure still closes transport");
    check(report.endpoint().dlcTxBytes() == 0, "report preflight failure sends no DLC bytes");
    bridge::NativeTransport slow;
    a::Runner stale(slow, config("observation-assertion"));
    stale.start(0);
    for (Time now = 0; now <= 20000 && !stale.done(); now += 2) {
        if (std::string(stale.phaseName()) == "normal_freshness_observation")
            slow.setUsbDelay(700);
        stale.tick(now);
        if (now % 20 == 0)
            stale.flushRecording(std::chrono::seconds(5));
    }
    check(stale.done() && stale.exitCode() == a::ExitCode::Assertion,
          "over-budget observation fails honestly");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--report-dir")
            root = std::filesystem::path(reinterpret_cast<const char8_t *>(argv[2]));
        else if (argc != 1)
            throw std::runtime_error("usage: acceptance_tests [--report-dir directory]");
        std::filesystem::create_directories(root);
        success();
        rejection();
        cancellationAndIo();
        std::cout << assertions << " acceptance assertions PASS; reports: " << root.string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
