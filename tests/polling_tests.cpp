#include "bridge/client.hpp"
#include "bridge/native_transport.hpp"
#include "honda_dlc/session.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
using namespace hd;
namespace b = hd::bridge;
namespace d = hd::dlc;
unsigned assertions{};
void check(bool condition, const char *expression, int line) {
    ++assertions;
    if (!condition)
        throw std::runtime_error(std::to_string(line) + ": " + expression);
}
#define CHECK(...) check(bool((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
struct Distribution {
    std::vector<Time> values;
    void add(Time n) { values.push_back(n); }
    void json(std::ostream &o) const {
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        o << "{\"n\":" << sorted.size();
        if (sorted.empty())
            o << ",\"min\":null,\"median\":null,\"p95\":null,\"max\":null}";
        else
            o << ",\"min\":" << sorted.front() << ",\"median\":" << sorted[(sorted.size() - 1) / 2]
              << ",\"p95\":" << sorted[(sorted.size() * 95 + 99) / 100 - 1] << ",\"max\":" << sorted.back()
              << '}';
    }
};
struct ChannelReport {
    std::uint64_t accepted{}, valid{}, staleEvents{}, staleMs{}, hiddenEvents{};
    std::optional<Time> lastStart, lastUpdate;
    Quality previous{Quality::NoData};
    bool hidden{};
    Time maximumAge{};
    Distribution starts, updates, ageAtReceipt, scheduleDelay;
};
struct Report {
    std::array<ChannelReport, ChannelCount> channels;
    std::uint64_t accepted{};
};
Report measure(const std::filesystem::path &path, bool fixed, Time usbDelay = 0,
               std::vector<Time> ticks = {1}, const char *scenario = "native-normal-1ms-tick",
               Time durationMs = 600000, bool overloaded = false) {
    b::NativeTransport native({0xfffff000, usbDelay, 7});
    b::Client client(native);
    auto settings = fixed ? d::bridgePollingSettings() : d::Settings{};
    if (overloaded)
        settings.bridgePolling->requestedIntervalsMs = {100, 100, 1000};
    d::Session session(client, settings, fixed ? d::bridgeFreshness() : FreshnessSettings{});
    std::array<ChannelReport, ChannelCount> channels;
    std::optional<Time> window;
    Distribution duration, firmwareDuration;
    std::size_t transportMax{}, endpointMax{}, parserMax{};
    std::uint64_t accepted{};
    std::uint64_t discardedResults{};
    std::optional<Time> lastAnyStart;
    session.onRaw = [&](const RawEvent &e) {
        if (window && e.kind == "bridge_ignored")
            ++discardedResults;
        if (window && e.kind == "bridge_result" && e.bridge)
            firmwareDuration.add(e.bridge->txElapsedMs + e.bridge->rxElapsedMs);
        if (e.kind != "tx_queued" || e.bytes.size() != 5)
            return;
        for (const auto &f : d::fields())
            if (f.read.address == e.bytes[2]) {
                auto &c = channels[channelIndex(f.channel)];
                if (window && c.lastStart) {
                    c.starts.add(e.time - *c.lastStart);
                    const auto planned = fixed ? *lastAnyStart + d::BridgeSlotMs
                                               : *c.lastStart + (f.channel == Channel::Coolant ? 1000 : 100);
                    c.scheduleDelay.add(e.time > planned ? e.time - planned : 0);
                }
                c.lastStart = e.time;
            }
        lastAnyStart = e.time;
    };
    session.onSample = [&](const Sample &s) {
        if (!s.request)
            return;
        for (const auto &f : d::fields()) {
            const auto i = channelIndex(f.channel);
            if (!(s.updatedMask & (1u << i)))
                continue;
            auto &c = channels[i];
            if (window) {
                ++c.accepted;
                ++accepted;
                if (s.qualities[i] == Quality::Valid)
                    ++c.valid;
                if (c.lastUpdate)
                    c.updates.add(s.time - *c.lastUpdate);
                c.ageAtReceipt.add(s.time - s.freshnessSince.value_or(s.time));
                duration.add(s.time - *c.lastStart);
            }
            c.lastUpdate = s.time;
        }
    };
    client.connect(0);
    for (Time now = 0; now <= usbDelay + 10; ++now)
        session.tick(now);
    if (client.state() != b::State::Ready)
        throw std::runtime_error("baseline handshake");
    session.start(usbDelay + 11);
    Time previousNow = usbDelay + 11;
    std::size_t tickIndex{};
    for (Time now = usbDelay + 12; !window || now <= *window + durationMs;) {
        if (window) {
            for (const auto &f : d::fields()) {
                auto &c = channels[channelIndex(f.channel)];
                const auto &m = session.model().channels()[channelIndex(f.channel)];
                if (m.lastValid) {
                    const auto threshold =
                        *m.lastValid + session.model().freshness().effective(f.channel).staleMs + 1;
                    const auto from = std::max(previousNow, threshold);
                    if (now > from)
                        c.staleMs += now - from;
                    c.maximumAge = std::max(c.maximumAge, now - *m.lastValid - 1);
                }
            }
        }
        session.tick(now);
        if (session.state() == d::State::Faulted)
            throw std::runtime_error(session.error());
        const auto &model = session.model();
        if (!window) {
            bool filled = true;
            for (const auto &f : d::fields())
                filled &= model.channels()[channelIndex(f.channel)].lastValid.has_value();
            if (filled)
                window = now;
            if (now > 8000 && !window)
                throw std::runtime_error("initial fill");
        }
        if (window) {
            for (const auto &f : d::fields()) {
                const auto i = channelIndex(f.channel);
                auto &c = channels[i];
                const auto &m = model.channels()[i];
                if (m.lastValid)
                    c.maximumAge = std::max(c.maximumAge, now - *m.lastValid);
                if (m.quality == Quality::Stale) {
                    if (c.previous != Quality::Stale)
                        ++c.staleEvents;
                }
                const bool hidden = !model.current(f.channel, now).has_value();
                if (hidden && !c.hidden)
                    ++c.hiddenEvents;
                c.hidden = hidden;
                c.previous = m.quality;
            }
            transportMax = std::max(transportMax, native.pendingBytes());
            endpointMax = std::max(endpointMax, native.endpoint().buffered());
            parserMax = std::max(parserMax, client.pendingBytes() - native.pendingBytes());
        }
        previousNow = now;
        if (window && now == *window + durationMs)
            break;
        now += ticks[tickIndex++ % ticks.size()];
        if (window)
            now = std::min(now, *window + durationMs);
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out.imbue(std::locale::classic());
    out << "{\n\"scenario\":\"" << scenario << "\",\"policy\":\""
        << (fixed ? d::BridgeSchedulerPolicy : "m2b-legacy")
        << "\","
           "\"baseline_source_sha\":\"1644a7296febb5d190e117237d6c5c7ab069d0b6\","
           "\"time_domain\":\"controlled-host-and-independent-device\",\"window_start_ms\":"
        << *window << ",\"window_duration_ms\":" << durationMs
        << ",\"initial_no_data_excluded\":true,"
           "\"percentile\":\"nearest rank ceil(p*n), median p=0.5\",\"accepted\":"
        << accepted << ",\"transactions_per_second\":" << accepted * 1000.0 / durationMs
        << ",\"host_request_to_result_ms\":";
    duration.json(out);
    out << ",\"firmware_operation_ms\":";
    firmwareDuration.json(out);
    out << ",\"first_correct_dlc_payload_ms\":null,\"first_payload_note\":\"not measured by protocol "
           "v1\",\"observation_window_ms\":200";
    out << ",\"timeouts\":" << session.stats().timeouts << ",\"faults\":" << session.stats().corrupt
        << ",\"discarded_results\":" << discardedResults << ",\"discarded_bytes\":" << session.stats().ignored
        << ",\"max_queues\":{\"scope\":\"entire session including handshake\",\"transport_bytes\":"
        << native.queueMetrics().usbDeliveryBytes
        << ",\"endpoint_rx_bytes\":" << native.queueMetrics().outerRxBytes
        << ",\"endpoint_tx_bytes\":" << native.queueMetrics().outerTxBytes
        << ",\"dlc_rx_bytes\":" << native.queueMetrics().dlcRxBytes
        << ",\"host_parser_sampled_after_tick_bytes\":" << parserMax << "},\"channels\":{\n";
    bool first = true;
    for (const auto &f : d::fields()) {
        if (!first)
            out << ",\n";
        first = false;
        const auto &c = channels[channelIndex(f.channel)];
        out << '"'
            << (f.channel == Channel::Rpm        ? "rpm"
                : f.channel == Channel::Throttle ? "tps"
                                                 : "ect")
            << "\":{\"accepted\":" << c.accepted << ",\"valid_decoded\":" << c.valid
            << ",\"achieved_hz\":" << c.accepted * 1000.0 / durationMs << ",\"requested_interval_ms\":"
            << session.metrics().channels[channelIndex(f.channel)].requestedIntervalMs
            << ",\"requested_period_misses\":"
            << session.metrics().channels[channelIndex(f.channel)].missedRequestedPeriods
            << ",\"stale_threshold_ms\":" << session.model().freshness().effective(f.channel).staleMs
            << ",\"hide_threshold_ms\":" << session.model().freshness().effective(f.channel).hideMs
            << ",\"start_interval_ms\":";
        c.starts.json(out);
        out << ",\"accepted_interval_ms\":";
        c.updates.json(out);
        out << ",\"age_at_receipt_ms\":";
        c.ageAtReceipt.json(out);
        out << ",\"schedule_delay_ms\":";
        c.scheduleDelay.json(out);
        out << ",\"maximum_age_ms\":" << c.maximumAge << ",\"stale_events\":" << c.staleEvents
            << ",\"stale_duration_ms\":" << c.staleMs << ",\"hidden_events\":" << c.hiddenEvents << '}';
    }
    out << "\n}}\n";
    if (!out)
        throw std::runtime_error("baseline report write");
    std::cout << "Timing report: " << path << "; ECT stale " << channels[2].staleMs << " ms\n";
    return {channels, accepted};
}
void schedulerAndStatistics() {
    d::BridgeScheduler scheduler;
    scheduler.reset(100, {});
    CHECK(!scheduler.selected(99));
    CHECK(scheduler.selected(100) == 0);
    CHECK(scheduler.selected(10000) == 0); // A failed submission does not consume a slot.
    scheduler.submitted(10000);
    CHECK(!scheduler.selected(10001));
    CHECK(scheduler.selected(10320) == 1);
    scheduler.submitted(10320);
    CHECK(scheduler.selected(10640) == 2); // ECT gets its finite share even under overload.
    d::BoundedDistribution distribution;
    CHECK(distribution.summary().n == 0 && !distribution.summary().p95);
    for (Time value = 1; value <= 1000; ++value)
        distribution.add(value);
    const auto summary = distribution.summary();
    CHECK(summary.n == 256 && summary.observations == 1000);
    CHECK(summary.min == 745 && summary.median == 872 && summary.p95 == 988 && summary.max == 1000);
    for (const auto channel : {Channel::Rpm, Channel::Throttle, Channel::Coolant}) {
        const auto threshold = d::bridgeFreshness().effective(channel);
        CHECK(threshold.staleMs >=
              d::bridgeMaximumRequestInterval(channel) + d::BridgeResultBudgetMs + d::BridgeReserveMs);
        CHECK(threshold.hideMs == threshold.staleMs * 3);
    }
    // Explicit opt-in is required. Direct offline ignores bridge scheduling even
    // if the caller supplies the setting; M0/M1 do not depend on this class.
    d::Session direct(d::bridgePollingSettings());
    direct.start(0);
    for (Time t = 0; t <= 1000; ++t)
        direct.tick(t);
    CHECK(direct.stats().accepted > 10);
}
class RejectingLink final : public d::Link {
  public:
    unsigned attempts{};
    void start(std::uint32_t, Time now, d::LinkCallbacks callbacks) override { callbacks.ready(now); }
    bool execute(std::span<const std::uint8_t>, d::Read, std::uint32_t, Time) override {
        ++attempts;
        return false; // Executor fails after Session's canExecute/whitelist checks.
    }
    void abort(Time) override {}
    void tick(Time) override {}
    std::size_t pendingBytes() const override { return 0; }
    bool deviceTimed() const override { return true; }
};
void rejectedSubmissionAndMetricBoundaries() {
    RejectingLink link;
    d::Session session(link, d::bridgePollingSettings(), d::bridgeFreshness());
    session.start(0);
    session.tick(0);
    CHECK(link.attempts == 1 && session.state() == d::State::Faulted);
    CHECK(session.metrics().submitted == 0 && session.metrics().rejectedSubmissions == 1);
    CHECK(!session.metrics().channels[0].lastStart && session.metrics().channels[0].submitted == 0);
    session.tick(10000);
    CHECK(link.attempts == 1); // No polling retry after loss of context.

    Model model;
    d::TimingMetrics metrics;
    Sample sample;
    sample.time = 200;
    sample.freshnessSince = 0;
    sample.updatedMask = 1;
    sample.qualities[0] = Quality::Valid;
    sample.values[0] = 750;
    model.apply(sample);
    metrics.observe(model, 200, 0);
    model.refresh(1000);
    metrics.observe(model, 1000, 0);
    CHECK(metrics.channels[0].staleEvents == 0 && metrics.channels[0].staleDurationMs == 0);
    model.refresh(1001);
    metrics.observe(model, 1001, 0);
    CHECK(metrics.channels[0].staleEvents == 1 && metrics.channels[0].staleDurationMs == 0);
    model.refresh(1200);
    metrics.observe(model, 1200, 0);
    sample.time = 1200;
    sample.freshnessSince = 1000;
    model.apply(sample);
    metrics.observe(model, 1200, 0);
    CHECK(metrics.channels[0].staleEvents == 1 && metrics.channels[0].staleDurationMs == 199);
    sample.time = 1500;
    sample.qualities[0] = Quality::Invalid;
    model.apply(sample);
    metrics.observe(model, 1500, 0);
    model.refresh(8000);
    metrics.observe(model, 8000, 0);
    CHECK(metrics.channels[0].staleDurationMs == 199); // Invalid is not a Stale interval.
}
struct Fixture {
    b::NativeTransport native;
    b::Client client{native};
    d::Session session;
    Time now{};
    explicit Fixture(FreshnessSettings freshness = d::bridgeFreshness(),
                     d::Settings settings = d::bridgePollingSettings())
        : session(client, settings, freshness) {
        client.connect(0);
        advance(10);
        CHECK(client.state() == b::State::Ready);
        session.start(++now);
    }
    void advance(Time elapsed) {
        const auto end = now + elapsed;
        while (now < end)
            session.tick(++now);
    }
    void fill() {
        for (unsigned i = 0; i < 5000; ++i) {
            advance(1);
            if (session.model().channels()[2].lastValid)
                return;
        }
        throw std::runtime_error("fixture fill timeout");
    }
};
void delayedAndFaultedResults() {
    Fixture stale({500, 1500}, {200, 50, 10000, 10000});
    stale.native.setUsbDelay(400);
    // Delay applies after NEW/CONFIG as well; wait for a condition, not an exact wake-up.
    while (stale.now < 5000 && !stale.session.model().channels()[0].lastValid)
        stale.advance(1);
    const auto &old = stale.session.model().channels()[0];
    CHECK(old.lastValid && old.quality == Quality::Stale);
    CHECK(stale.now - *old.lastValid >= 600 && old.value == 750);
    CHECK(stale.session.metrics().channels[0].lastRequestToResultMs >= 600);

    Fixture excessive;
    excessive.fill();
    excessive.native.setUsbDelay(1001);
    const auto ectStamp = excessive.session.model().channels()[2].lastValid;
    for (unsigned i = 0; i < 4000 && excessive.session.state() != d::State::Faulted; ++i)
        excessive.advance(1);
    CHECK(excessive.session.state() == d::State::Faulted);
    CHECK(excessive.session.model().channels()[2].lastValid == ectStamp);
    const auto transmitted = excessive.native.endpoint().dlcTxBytes();
    excessive.advance(6500);
    CHECK(excessive.native.endpoint().dlcTxBytes() == transmitted);
    CHECK(!excessive.session.model().current(Channel::Coolant, excessive.now));
    CHECK(!excessive.session.request(d::Operation::Read, {0x14, 1}, excessive.now));

    Fixture controls(d::bridgeFreshness(), {200, 50, 10000, 10000});
    controls.fill();
    const auto stamps = controls.session.model().channels();
    const auto before = controls.session.metrics().submitted;
    CHECK(!controls.session.request(d::Operation::Write, {0, 2}, controls.now));
    CHECK(controls.session.metrics().submitted == before);
    controls.session.setScenario(d::Scenario::Higher);
    controls.advance(20); // CONFIG ACK carries no readings.
    CHECK(controls.session.model().channels()[2].lastValid == stamps[2].lastValid);
    CHECK(controls.session.request(d::Operation::Read, {0, 2}, ++controls.now));
    controls.advance(250);
    CHECK(controls.session.model().channels()[0].value == 1500);
    CHECK(controls.session.model().channels()[2].lastValid == stamps[2].lastValid);
    controls.session.stop(++controls.now);
    CHECK(!controls.session.model().channels()[0].lastValid);
    controls.client.disconnect(controls.now);
    controls.client.connect(++controls.now);
    controls.advance(10);
    CHECK(controls.client.state() == b::State::Ready);
    CHECK(!controls.session.model().channels()[0].lastValid);
    controls.session.start(++controls.now);
    controls.fill();
    CHECK(controls.session.model().channels()[0].value == 1500);
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::filesystem::path report = "build/reports";
        if (argc == 3 && std::string(argv[1]) == "--report-dir")
            report = argv[2];
        else if (argc != 1)
            throw std::runtime_error("usage: polling_tests [--report-dir directory]");
        const auto baseline = measure(report / "baseline-normal.json", false);
        const auto after = measure(report / "after-normal.json", true);
        CHECK(baseline.channels[2].staleEvents > 500 && baseline.channels[2].staleMs > 100000);
        CHECK(baseline.accepted == 2985);
        for (const auto channel : {Channel::Rpm, Channel::Throttle, Channel::Coolant}) {
            const auto &c = after.channels[channelIndex(channel)];
            CHECK(c.staleEvents == 0 && c.staleMs == 0 && c.hiddenEvents == 0);
            CHECK(c.accepted == c.valid);
        }
        const auto oldJitter = measure(report / "baseline-budget-jitter-usb50.json", false, 50,
                                       {1, 7, 20, 3, 13}, "native-ticks<=20-usb50");
        const auto jitter = measure(report / "after-budget-jitter-usb50.json", true, 50, {1, 7, 20, 3, 13},
                                    "native-ticks<=20-usb50");
        CHECK(oldJitter.channels[2].staleMs > 0);
        for (const auto channel : {Channel::Rpm, Channel::Throttle, Channel::Coolant}) {
            const auto &c = jitter.channels[channelIndex(channel)];
            CHECK(c.staleEvents == 0 && c.staleMs == 0 && c.hiddenEvents == 0);
        }
        const auto over = measure(report / "after-overload.json", true, 0, {1},
                                  "native-overloaded-requested-goals", 60000, true);
        CHECK(over.channels[2].accepted >= 35);
        CHECK(over.channels[0].accepted >= over.channels[2].accepted * 2 - 2);
        CHECK(over.channels[0].starts.values.front() >= 640);
        const auto slow = measure(report / "after-outside-budget-usb600.json", true, 600, {1},
                                  "native-usb600-outside-budget", 60000);
        CHECK(slow.channels[2].staleMs > 0 && slow.channels[0].staleMs > 0);
        schedulerAndStatistics();
        rejectedSubmissionAndMetricBoundaries();
        delayedAndFaultedResults();
        std::cout << "polling assertions: " << assertions << " PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
