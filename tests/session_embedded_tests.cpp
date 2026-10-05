#include "application/session.hpp"
#include "endpoint.hpp"
#include "recording/recorder.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace hd;
namespace {
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
// Same endpoint.cpp compiled for AVR. Fragment RX/TX and use wrapping firmware
// clock with an unrelated epoch to the PC's monotonic timestamps.
struct EmbeddedTransport : Transport {
    hd_nano::Endpoint endpoint;
    TransportCallbacks cb;
    bool open{};
    std::uint32_t offset{0xFFFFF000u};
    void start(TransportCallbacks c, Time n) override {
        close();
        cb = std::move(c);
        open = true;
        endpoint.reset(clock(n));
        cb.opened(n);
    }
    void close() override {
        open = false;
        cb = {};
    }
    std::uint32_t clock(Time n) { return static_cast<std::uint32_t>(n) + offset; }
    bool send(std::span<const std::uint8_t> b, Time n) override {
        if (!open)
            return false;
        cb.raw("TX", b, n);
        for (auto v : b)
            endpoint.receive(v, clock(n));
        cb.sent(n);
        return true;
    }
    void tick(Time n) override {
        endpoint.tick(clock(n));
        std::uint8_t b[7];
        auto count = endpoint.read(b, sizeof b);
        if (count) {
            cb.raw("RX", {b, count}, n);
            cb.received({b, count}, n);
        }
    }
    bool isOpen() const override { return open; }
    std::size_t pendingBytes() const override { return 0; }
    std::string name() const override { return "embedded-test"; }
};
void advance(Session &s, Time &n, Time d) {
    auto end = n + d;
    for (; n < end; ++n)
        s.tick(n);
    s.tick(n);
}
} // namespace
int main() {
    try {
        EmbeddedTransport transport;
        std::uint32_t ids = 500;
        Session s(transport, {}, {}, [&] { return ++ids; });
        Time n = 0;
        const std::array<double, 7> values{3210, 42.5, 80, -12.3, 25, 100, 14.2};
        s.setScenario(3, 123);
        s.setManual(values);
        auto root = std::filesystem::temp_directory_path() / "hondadash-embedded-recording";
        Recorder recorder(4096);
        check(recorder.start(root,
                             {"manual", 123, "serial", "nano-synthetic", "0.2.0", "test-adapter", 115200}),
              "recorder start");
        s.onRaw = [&](const RawEvent &e) { check(recorder.enqueueRaw(e), "record raw"); };
        s.onSample = [&](const Sample &a) { check(recorder.enqueueSample(a), "record sample"); };
        s.start(n);
        advance(s, n, 350);
        check(s.state() == SessionState::Running && s.deviceInfo()->kind == 2, "real embedded HELLO/info");
        for (unsigned i = 0; i < 7; ++i)
            check(s.model().current(static_cast<Channel>(i), n) == values[i],
                  "independent known seven-channel scaled values");
        auto accepted = s.stats().accepted;
        auto newValues = values;
        newValues[0] = 4567;
        s.setManual(newValues);
        s.tick(n);
        check(s.stats().accepted == accepted && s.model().current(Channel::Rpm, n) == 3210,
              "ACK is not measurement");
        advance(s, n, 300);
        check(s.model().current(Channel::Rpm, n) == 4567, "manual via AVR dispatcher and next snapshot");
        s.setFaults({true, 0, false, false});
        advance(s, n, 2000);
        check(s.model().channels()[0].quality == Quality::Stale, "embedded silence stale");
        advance(s, n, 2000);
        check(!s.model().current(Channel::Rpm, n), "hidden across millis wrap");
        s.setFaults({});
        advance(s, n, 600);
        check(s.model().current(Channel::Rpm, n) == 4567, "wire restoration after wrap");
        s.setFaults({false, 1000, false, false});
        advance(s, n, 2000);
        check(s.state() == SessionState::Running && s.stats().ignored > 0,
              "bounded delayed snapshot Busy is recoverable");
        s.setFaults({});
        advance(s, n, 1600);
        check(s.model().current(Channel::Rpm, n) == 4567, "restore while delayed snapshot pending");
        auto before = s.id();
        transport.endpoint.reset(transport.clock(n));
        advance(s, n, 700);
        check(s.id() != before && s.state() == SessionState::Running &&
                  s.model().current(Channel::Rpm, n) == 4567,
              "firmware reset re-HELLO and desired control replay");
        recorder.stop();
        s.onRaw = {};
        s.onSample = {};
        std::ifstream raw(recorder.directory() / "raw.jsonl");
        std::string header;
        std::getline(raw, header);
        check(header.find("\"transport\":\"serial\"") != std::string::npos &&
                  header.find("\"source\":\"simulation\"") != std::string::npos &&
                  header.find("nano-synthetic") != std::string::npos,
              "recorded synthetic serial metadata");
        s.setFaults({false, 0, true, true});
        advance(s, n, 900);
        check(s.state() == SessionState::Running && s.stats().timeouts > 0, "one-shot fault recovers");
        for (unsigned i = 0; i < 100; ++i) {
            auto old = s.id();
            s.start(n);
            check(s.id() != old && !s.model().current(Channel::Rpm, n), "reconnect NoData");
            advance(s, n, 250);
            check(s.model().current(Channel::Rpm, n) == 4567, "reconnect restored");
        }
        advance(s, n, 600000);
        check(s.stats().accepted > 5900 && transport.endpoint.buffered() <= 79 &&
                  transport.endpoint.available() <= 158,
              "embedded sustained virtual time load");
        std::cout
            << "session_embedded: real AVR source end-to-end, wrap/reset/controls/recording/load passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
