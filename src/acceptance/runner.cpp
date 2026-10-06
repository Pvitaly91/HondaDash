#include "acceptance/runner.hpp"
#include "../../firmware/nano_dlc_bridge_lab/wire.hpp"
#include "../../firmware/shared/bench_wire.hpp"
#include "honda_dlc/polling.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace hd::acceptance {
namespace {
constexpr std::array<Channel, 3> Channels{Channel::Rpm, Channel::Coolant, Channel::Throttle};
constexpr std::array<double, 3> A{750, 61, 32}, B{1500, 89, 75};
enum class Phase { Idle, Handshake, A, B, Observe, Fault, Aging, Recovery, Closing, Done };
const char *name(Phase phase) {
    switch (phase) {
    case Phase::Idle:
        return "idle";
    case Phase::Handshake:
        return "handshake";
    case Phase::A:
        return "raw_A";
    case Phase::B:
        return "raw_B";
    case Phase::Observe:
        return "normal_freshness_observation";
    case Phase::Fault:
        return "controlled_DLC_checksum_fault";
    case Phase::Aging:
        return "fault_drain_stale_hide_no_polling";
    case Phase::Recovery:
        return "explicit_new_experiment_recovery";
    case Phase::Done:
        return "done";
    case Phase::Closing:
        return "quiesce_and_close_both_devices";
    }
    return "unknown";
}
std::string quote(std::string_view value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            out << '\\' << static_cast<char>(c);
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else
            out << static_cast<char>(c);
    }
    return out.str() + '"';
}
std::string pathText(const std::filesystem::path &path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}
struct Distribution {
    std::array<Time, 256> values{};
    std::uint64_t count{};
    Time minimum{}, maximum{};
    void add(Time value) {
        if (!count)
            minimum = maximum = value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        values[count++ % values.size()] = value;
    }
    void json(std::ostream &out) const {
        const auto retained = static_cast<std::size_t>(std::min<std::uint64_t>(count, values.size()));
        out << "{\"n\":" << count << ",\"retained\":" << retained;
        if (!count) {
            out << ",\"min\":null,\"median\":null,\"p95\":null,\"max\":null}";
            return;
        }
        std::vector<Time> sorted(values.begin(), values.begin() + retained);
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double p) {
            return sorted[static_cast<std::size_t>(std::ceil(p * sorted.size())) - 1];
        };
        out << ",\"min\":" << minimum << ",\"median\":" << percentile(.5) << ",\"p95\":" << percentile(.95)
            << ",\"max\":" << maximum << '}';
    }
};
struct Metrics {
    std::uint64_t accepted{}, valid{}, staleTransitions{}, hiddenTransitions{};
    Time staleMs{}, maxAge{};
    std::optional<Time> lastRequest, lastReceipt;
    bool wasStale{}, wasHidden{};
    Distribution requestIntervals, updateIntervals, ageAtReceipt, hostRoundtrip, firmwareOperation;
};
struct Step {
    std::string name, result, detail;
    Time start{}, end{};
};
void summaryJson(std::ostream &out, const dlc::DistributionSummary &summary) {
    const auto number = [](std::optional<Time> value) { return value ? std::to_string(*value) : "null"; };
    out << "{\"observations\":" << summary.observations << ",\"n\":" << summary.n
        << ",\"min\":" << number(summary.min) << ",\"median\":" << number(summary.median)
        << ",\"p95\":" << number(summary.p95) << ",\"max\":" << number(summary.max) << '}';
}
void driverJson(std::ostream &out, const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    if (bytes.size() != offset + 22) {
        out << "null";
        return;
    }
    constexpr std::array<const char *, 11> names{"rx_bytes",   "tx_bytes",    "echo_bytes",  "false_starts",
                                                 "framing",    "rx_overflow", "tx_overflow", "stuck_low",
                                                 "collisions", "timing",      "line_busy"};
    out << '{';
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i)
            out << ',';
        out << quote(names[i]) << ':' << hd_bridge::load16(bytes.data() + offset + 2 * i);
    }
    out << '}';
}
} // namespace

struct Runner::Impl {
    Transport &transport;
    Transport *responderTransport;
    Config config;
    Recorder recorder;
    std::unique_ptr<bridge::Client> virtualClient;
    std::unique_ptr<bench::Controller> benchController;
    const bridge::Client &client;
    dlc::Session session;
    Phase phase{Phase::Idle};
    ExitCode code{ExitCode::Assertion};
    std::string message{"not started"}, recordingFailure;
    std::optional<bridge::Info> identity, peerIdentity;
    Time started{}, phaseAt{}, lastTick{}, observedAt{}, observedUntil{};
    std::array<Metrics, 3> metrics{};
    std::array<dlc::DistributionSummary, 3> scheduleDelays{};
    std::array<bool, 3> seen{};
    std::vector<Step> steps;
    std::vector<RawEvent> handshakeTrace;
    std::uint64_t acceptedTotal{}, readIntents{}, intentsAtFault{}, discarded{}, faultCount{},
        expectedFaultCount{};
    std::uint64_t usbTx{}, usbRx{}, innerTx{}, innerRx{}, lateRx{}, responderTx{}, responderRx{};
    std::size_t maxPending{};
    bool recordingStarted{}, finishing{};

    Impl(Transport &value, Transport *peer, Config settings, Recorder::WriteHook hook)
        : transport(value), responderTransport(peer), config(std::move(settings)),
          recorder(256, std::move(hook)),
          virtualClient(peer ? nullptr : std::make_unique<bridge::Client>(value, config.bridgeSettings)),
          benchController(peer ? std::make_unique<bench::Controller>(value, *peer, config.bridgeSettings)
                               : nullptr),
          client(peer ? benchController->bridgeClient() : *virtualClient),
          session(peer ? static_cast<dlc::Link &>(*benchController)
                       : static_cast<dlc::Link &>(*virtualClient),
                  dlc::bridgePollingSettings(), dlc::bridgeFreshness()) {
        if (benchController)
            benchController->onRaw = [this](const RawEvent &event) { raw(event); };
        else
            virtualClient->onRaw = [this](const RawEvent &event) { raw(event); };
        session.onRaw = [this](const RawEvent &event) { raw(event); };
        session.onSample = [this](const Sample &sample) { sampleReceived(sample); };
        handshakeTrace.reserve(64);
        steps.reserve(8);
    }
    bool beginRecording() {
        if (recordingStarted)
            return true;
        RecordingMetadata metadata;
        metadata.formatVersion = 3;
        metadata.scenario = "acceptance A/B/checksum fault/explicit recovery";
        metadata.transport = transport.name();
        metadata.endpoint = identity ? identity->identity : "unrecognized endpoint";
        metadata.firmware = identity ? identity->firmware : "unverified";
        metadata.port = config.port;
        metadata.baud = config.backend != Backend::Native ? 115200 : 0;
        metadata.wireProtocol = "honda-dlc";
        metadata.profile = dlc::ProfileId;
        metadata.profileVersion = dlc::ProfileVersion;
        metadata.evidenceStatus = "reference-derived; no ECU captures";
        metadata.fixtureClass = "reference-derived and synthetic-fault";
        metadata.fixtureId = "M2a A/B raw fixtures";
        metadata.freshness = session.model().freshness();
        metadata.bridgeIdentity = identity ? identity->identity : "unverified";
        metadata.bridgeVersion = identity ? identity->protocolVersion : 0;
        metadata.readPolicyVersion = identity ? identity->policyVersion : 0;
        metadata.backend = identity ? (benchController ? "two-nano-bench" : "virtual") : "unverified";
        metadata.outerProtocol = benchController ? hd_bench::BridgeIdentity : hd_bridge::Identity;
        metadata.schedulerPolicy = dlc::BridgeSchedulerPolicy;
        metadata.schedulerPolicyVersion = dlc::BridgeSchedulerVersion;
        metadata.requestedIntervals = dlc::bridgeRequestedIntervals();
        metadata.freshnessPolicy = dlc::BridgeFreshnessPolicy;
        metadata.freshnessPolicyVersion = dlc::BridgeFreshnessVersion;
        metadata.timingEstimateSource = "host_request_start_lower_bound";
        metadata.measurementScope = "virtual bridge acceptance; physical hardware unverified";
        if (benchController) {
            const auto &peer = benchController->responderInfo();
            metadata.benchSchemaVersion = 1;
            metadata.benchIoEnabled = true;
            metadata.vehicleConnectionAllowed = false;
            metadata.responderIdentity = peer ? peer->identity : "unverified";
            metadata.responderFirmware = peer ? peer->firmware : "unverified";
            metadata.responderProtocolVersion = peer ? peer->protocolVersion : 0;
            metadata.responderReadPolicyVersion = peer ? peer->policyVersion : 0;
            metadata.responderPort = config.responderPort;
            metadata.firmwareTarget = "bridge-bench + responder-bench";
            metadata.measurementScope =
                "two-Nano low-voltage bench path; software model is not physical hardware verification";
        }
        recordingStarted = recorder.start(config.reportDirectory / "journals", metadata);
        if (!recordingStarted) {
            recordingFailure = recorder.error();
            return false;
        }
        for (const auto &event : handshakeTrace)
            if (!recorder.enqueueRaw(event))
                recordingFailure = recorder.error();
        handshakeTrace.clear();
        return recordingFailure.empty();
    }
    void raw(const RawEvent &event) {
        if (event.kind == "usb_tx")
            usbTx += event.bytes.size();
        if (event.kind == "usb_rx")
            usbRx += event.bytes.size();
        if (event.kind == "responder_usb_tx")
            responderTx += event.bytes.size();
        if (event.kind == "responder_usb_rx")
            responderRx += event.bytes.size();
        if (event.kind == "bridge_dlc_tx")
            innerTx += event.bytes.size();
        if (event.kind == "bridge_dlc_rx")
            innerRx += event.bytes.size();
        if (event.kind == "bridge_event")
            lateRx += event.bytes.size();
        if (event.kind == "bridge_ignored")
            ++discarded;
        if (event.kind == "bridge_fault")
            ++faultCount;
        if (event.kind == "tx_queued" && event.request)
            ++readIntents;
        if (recordingStarted) {
            if (!recorder.enqueueRaw(event))
                recordingFailure = recorder.error();
        } else if (handshakeTrace.size() < 128)
            handshakeTrace.push_back(event);
        else
            recordingFailure = "bounded handshake trace overflow";
    }
    void sampleReceived(const Sample &sample) {
        if (recordingStarted && !recorder.enqueueSample(sample))
            recordingFailure = recorder.error();
        if (!sample.request)
            return;
        ++acceptedTotal;
        const auto &expected = phase == Phase::A || phase == Phase::Recovery ? A : B;
        for (std::size_t i = 0; i < Channels.size(); ++i) {
            const auto index = channelIndex(Channels[i]);
            if (!(sample.updatedMask & (1u << index)))
                continue;
            if (sample.qualities[index] == Quality::Valid && sample.values[index] == expected[i])
                seen[i] = true;
            if (phase != Phase::Observe)
                continue;
            auto &value = metrics[i];
            ++value.accepted;
            if (sample.qualities[index] == Quality::Valid)
                ++value.valid;
            const auto requestAt = sample.freshnessSince.value_or(sample.time);
            if (value.lastRequest)
                value.requestIntervals.add(requestAt - *value.lastRequest);
            if (value.lastReceipt)
                value.updateIntervals.add(sample.time - *value.lastReceipt);
            value.lastRequest = requestAt;
            value.lastReceipt = sample.time;
            value.ageAtReceipt.add(sample.time - requestAt);
            value.hostRoundtrip.add(sample.time - requestAt);
            const auto &timing = client.diagnostics();
            value.firmwareOperation.add(Time(timing.txElapsedMs) + timing.rxElapsedMs);
        }
    }
    void enter(Phase next, Time now) {
        phase = next;
        phaseAt = now;
        seen.fill(false);
    }
    void pass(Time now, std::string detail) {
        steps.push_back({name(phase), "PASS", std::move(detail), phaseAt, now});
    }
    bool valuesMatch(Time now, const std::array<double, 3> &expected) const {
        for (std::size_t i = 0; i < Channels.size(); ++i)
            if (!seen[i] || session.model().channels()[channelIndex(Channels[i])].quality != Quality::Valid ||
                session.model().current(Channels[i], now) != expected[i])
                return false;
        return true;
    }
    void writeReport(Time now) {
        std::ofstream out(config.reportDirectory / "report.json", std::ios::binary | std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.imbue(std::locale::classic());
        out << std::setprecision(10)
            << "{\n\"format_version\":1,\"result\":" << quote(code == ExitCode::Success ? "PASS" : "FAIL")
            << ",\"exit_code\":" << static_cast<int>(code) << ",\"message\":" << quote(message)
            << ",\"desktop_version\":" << quote(config.desktopVersion)
            << ",\"desktop_sha\":" << quote(config.desktopSha) << ",\"os\":" << quote(config.os)
            << ",\"backend\":"
            << quote(config.backend == Backend::Native ? "native"
                     : benchController                 ? "two-nano-bench"
                                                       : "serial")
            << ",\"transport\":" << quote(transport.name()) << ",\"port\":" << quote(config.port)
            << ",\"baud\":" << (config.backend != Backend::Native ? 115200 : 0)
            << ",\"source\":\"simulation\",\"scope\":"
            << quote(benchController ? "production bench bridge and external responder path; test transport "
                                       "may be software bit-line model"
                                     : "production virtual bridge path; serial may be software PTY")
            << ",\"hardware_verified\":false,\"live_enabled\":false"
            << (benchController ? ",\"bench_schema_version\":1,\"bench_io_enabled\":true,\"vehicle_"
                                  "connection_allowed\":false"
                                : ",\"physical_dlc_enabled\":false")
            << ",\"firmware_identity\":" << (identity ? quote(identity->identity) : "null")
            << ",\"firmware_version\":" << (identity ? quote(identity->firmware) : "null")
            << ",\"protocol_version\":" << (identity ? std::to_string(identity->protocolVersion) : "null")
            << ",\"read_policy_version\":" << (identity ? std::to_string(identity->policyVersion) : "null")
            << ",\"profile\":" << quote(dlc::ProfileId) << ",\"profile_version\":" << dlc::ProfileVersion
            << ",\"scheduler_policy\":" << quote(dlc::BridgeSchedulerPolicy)
            << ",\"freshness_policy\":" << quote(dlc::BridgeFreshnessPolicy)
            << ",\"scheduler_version\":1,\"freshness_version\":1"
            << ",\"freshness_time_source\":\"host_request_start_lower_bound; not physical sensor "
               "acquisition\""
            << ",\"elapsed_host_ms\":" << now - started << ",\"observation_start_ms\":" << observedAt
            << ",\"observation_end_ms\":" << observedUntil
            << ",\"observation_window_ms\":" << (observedUntil >= observedAt ? observedUntil - observedAt : 0)
            << ",\"observation_start_rule\":\"first tick after all three accepted B channels are Valid\""
            << ",\"dlc_observation_guard_ms\":200,\"time_to_correct_dlc_payload_ms\":null"
            << ",\"time_to_correct_dlc_payload_note\":\"not measured by protocol v1\""
            << ",\"model_update_time\":\"Sample.time: host receipt, same Session receive callback\""
            << ",\"percentile_method\":\"nearest-rank ceil(p*n), latest 256; min/max/count cumulative; null "
               "when n=0\""
            << ",\"accepted_total\":" << acceptedTotal << ",\"faults\":" << faultCount
            << ",\"expected_dlc_faults\":" << expectedFaultCount << ",\"discarded_results\":" << discarded
            << ",\"max_pending_host_bytes\":" << maxPending << ",\"port_closed\":"
            << (!transport.isOpen() && (!responderTransport || !responderTransport->isOpen()) ? "true"
                                                                                              : "false")
            << ",\"journal_closed\":true,\"journal_error\":" << quote(recorder.error())
            << ",\"raw_jsonl\":" << quote(pathText(recorder.directory() / "raw.jsonl"))
            << ",\"measurements_csv\":" << quote(pathText(recorder.directory() / "measurements.csv"))
            << ",\"raw_bytes\":{\"usb_tx\":" << usbTx << ",\"usb_rx\":" << usbRx
            << ",\"bridge_reported_dlc_tx\":" << innerTx << ",\"bridge_reported_dlc_rx\":" << innerRx
            << ",\"unassociated_dlc_rx\":" << lateRx << '}';
        if (benchController) {
            out << ",\"responder_port\":" << quote(config.responderPort)
                << ",\"responder_identity\":" << (peerIdentity ? quote(peerIdentity->identity) : "null")
                << ",\"responder_firmware\":" << (peerIdentity ? quote(peerIdentity->firmware) : "null")
                << ",\"responder_protocol_version\":"
                << (peerIdentity ? std::to_string(peerIdentity->protocolVersion) : "null")
                << ",\"responder_read_policy_version\":"
                << (peerIdentity ? std::to_string(peerIdentity->policyVersion) : "null")
                << ",\"firmware_target\":\"bridge-bench + responder-bench\""
                << ",\"responder_control_rx_bytes\":" << responderRx
                << ",\"responder_control_tx_bytes\":" << responderTx;
            out << ",\"driver_counters\":{\"source\":\"MCU_reported; software model when using test "
                   "transport\",\"bridge\":";
            driverJson(out, client.deviceDiagnostics(), 30);
            out << ",\"responder\":";
            driverJson(out, benchController->responderDiagnostics(), 38);
            out << '}';
        }
        out << ",\n\"channels\":[";
        std::uint64_t acceptedObserved = 0;
        for (std::size_t i = 0; i < Channels.size(); ++i) {
            if (i)
                out << ',';
            const auto &value = metrics[i];
            acceptedObserved += value.accepted;
            const auto threshold = session.model().freshness().effective(Channels[i]);
            const auto requested = dlc::bridgeRequestedIntervals()[channelIndex(Channels[i])];
            out << "{\"channel\":" << quote(channelInfo(Channels[i]).name)
                << ",\"accepted\":" << value.accepted << ",\"valid_decoded\":" << value.valid
                << ",\"requested_interval_ms\":" << requested << ",\"requested_hz\":" << 1000.0 / requested
                << ",\"achieved_hz\":";
            if (observedUntil > observedAt)
                out << 1000.0 * value.accepted / (observedUntil - observedAt);
            else
                out << "null";
            out << ",\"stale_ms\":" << threshold.staleMs << ",\"hide_ms\":" << threshold.hideMs
                << ",\"stale_transitions\":" << value.staleTransitions
                << ",\"stale_duration_ms\":" << value.staleMs
                << ",\"hidden_transitions\":" << value.hiddenTransitions
                << ",\"maximum_age_ms\":" << value.maxAge << ",\"request_intervals_ms\":";
            value.requestIntervals.json(out);
            out << ",\"update_intervals_ms\":";
            value.updateIntervals.json(out);
            out << ",\"age_at_acceptance_ms\":";
            value.ageAtReceipt.json(out);
            out << ",\"host_request_to_result_ms\":";
            value.hostRoundtrip.json(out);
            out << ",\"firmware_reported_operation_ms\":";
            value.firmwareOperation.json(out);
            out << ",\"schedule_delay_ms\":";
            summaryJson(out, scheduleDelays[i]);
            out << ",\"schedule_delay_scope\":\"initial experiment through normal observation, latest 256\"";
            out << '}';
        }
        out << "],\"aggregate_observed_transactions_hz\":";
        if (observedUntil > observedAt)
            out << 1000.0 * acceptedObserved / (observedUntil - observedAt);
        else
            out << "null";
        out << ",\"scenarios\":[";
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (i)
                out << ',';
            const auto &step = steps[i];
            out << "{\"name\":" << quote(step.name) << ",\"result\":" << quote(step.result)
                << ",\"detail\":" << quote(step.detail) << ",\"start_ms\":" << step.start
                << ",\"end_ms\":" << step.end << '}';
        }
        out << "],\"unperformed_hardware_steps\":[\"physical Nano identity/board/USB chip\","
               "\"physical reset button\",\"physical USB unplug/replug\",\"electrical DLC interface\",\"real "
               "ECU\",\"two physical Nano signal exchange\",\"logic analyzer timing/levels/edges\"]}\n";
        out.flush();
        out.close();
    }
    void finish(ExitCode result, std::string detail, Time now, bool force = false) {
        if (phase == Phase::Done || finishing)
            return;
        if (phase == Phase::Closing && !force)
            return;
        if (!force && benchController && benchController->responderInfo() && client.info()) {
            code = result;
            message = std::move(detail);
            if (result != ExitCode::Success)
                steps.push_back({name(phase), "FAIL", message, phaseAt, now});
            session.stop(now);
            phase = Phase::Closing;
            phaseAt = now;
            return;
        }
        finishing = true;
        code = result;
        message = std::move(detail);
        if (result != ExitCode::Success)
            steps.push_back({name(phase), "FAIL", message, phaseAt, now});
        if (phase == Phase::Observe)
            observedUntil = now;
        if (!recordingStarted && code != ExitCode::Arguments && code != ExitCode::Report)
            beginRecording();
        if (phase != Phase::Closing)
            session.stop(now);
        if (benchController)
            benchController->disconnect(now);
        else
            virtualClient->disconnect(now);
        recorder.stop();
        if ((!recordingFailure.empty() || !recorder.error().empty()) && code != ExitCode::Report) {
            code = ExitCode::Recording;
            message = recordingFailure.empty() ? recorder.error() : recordingFailure;
        }
        try {
            if (code == ExitCode::Arguments) {
                // Invalid arguments must not open devices or create/overwrite a
                // report destination that has not passed the startup preflight.
            } else if (!config.reportDirectory.empty())
                writeReport(now);
            else if (code != ExitCode::Arguments)
                throw std::runtime_error("empty report directory");
        } catch (const std::exception &error) {
            code = ExitCode::Report;
            message = "Cannot write acceptance report: " + std::string(error.what());
        }
        phase = Phase::Done;
        finishing = false;
    }
    void tick(Time now) {
        if (phase == Phase::Done || phase == Phase::Idle)
            return;
        if (now < lastTick) {
            finish(ExitCode::Assertion, "host clock moved backwards", lastTick);
            return;
        }
        const auto delta = now - lastTick;
        lastTick = now;
        session.tick(now);
        if (phase == Phase::Closing) {
            if (benchController->quiescent()) {
                pass(now,
                     "Both devices stopped; responder physical TX completion acknowledged before USB close");
                finish(code, message, now, true);
            } else if (now - phaseAt >= 2 * config.bridgeSettings.acceptMs + 100) {
                finish(code == ExitCode::Success ? ExitCode::Timeout : code,
                       message + "; final responder quiescence not acknowledged", now, true);
            }
            return;
        }
        maxPending = std::max(maxPending, session.pendingBytes());
        if (!recordingFailure.empty() || !recorder.error().empty()) {
            finish(ExitCode::Recording, "recording failed", now);
            return;
        }
        if (now - started >= config.overallTimeoutMs) {
            finish(ExitCode::Timeout, "overall acceptance deadline", now);
            return;
        }
        if (phase != Phase::Observe && now - phaseAt >= config.phaseTimeoutMs) {
            finish(ExitCode::Timeout, std::string(name(phase)) + " deadline", now);
            return;
        }
        if ((client.state() == bridge::State::Faulted ||
             (benchController && benchController->state() == bench::State::Faulted) ||
             session.state() == dlc::State::Faulted) &&
            phase != Phase::Fault && phase != Phase::Aging) {
            const auto failure = benchController ? benchController->error() : client.error();
            finish(failure.find("watchdog") != std::string::npos ? ExitCode::Timeout : ExitCode::Endpoint,
                   "Unexpected bridge failure: " + failure + "; " + session.error(), now);
            return;
        }
        switch (phase) {
        case Phase::Handshake:
            if (client.state() != bridge::State::Ready ||
                (benchController && benchController->state() != bench::State::Ready))
                break;
            identity = client.info();
            if (benchController)
                peerIdentity = benchController->responderInfo();
            if ((!benchController &&
                 (!identity || identity->identity != hd_bridge::Identity || identity->protocolVersion != 1 ||
                  identity->policyVersion != 1 || identity->backend != 1 || identity->physicalDlcEnabled ||
                  identity->firmware != "1.0.0" || identity->capabilities != 7)) ||
                (benchController && (!identity || !peerIdentity || !identity->benchIoEnabled ||
                                     !peerIdentity->benchIoEnabled)) ||
                innerTx) {
                finish(ExitCode::Endpoint, "handshake identity/capability mismatch or unexpected DLC TX",
                       now);
                break;
            }
            if (!beginRecording()) {
                finish(ExitCode::Recording, "cannot start recording", now);
                break;
            }
            pass(now, benchController
                          ? "Both exact bench identities verified before any line operation"
                          : "Exact virtual-only identity verified; handshake performed no DLC operation");
            enter(Phase::A, now);
            if (benchController)
                benchController->onRaw = {};
            else
                virtualClient
                    ->onRaw = {}; // Session receives production timing facts and forwards raw events.
            session.setScenario(dlc::Scenario::Baseline);
            session.setFaults({});
            session.start(now);
            break;
        case Phase::A:
            if (valuesMatch(now, A)) {
                pass(now, "Accepted partial DLC bytes decoded to RPM=750 ECT=61 TPS=32");
                enter(Phase::B, now);
                session.setScenario(dlc::Scenario::Higher);
            }
            break;
        case Phase::B:
            if (valuesMatch(now, B)) {
                pass(now, "CONFIG changed raw fixtures; RPM=1500 ECT=89 TPS=75 accepted independently");
                enter(Phase::Observe, now);
                observedAt = now;
            }
            break;
        case Phase::Observe:
            for (std::size_t i = 0; i < Channels.size(); ++i) {
                auto &value = metrics[i];
                const auto &measurement = session.model().channels()[channelIndex(Channels[i])];
                const bool stale = measurement.quality == Quality::Stale;
                const bool hidden = !session.model().current(Channels[i], now);
                if (stale && !value.wasStale)
                    ++value.staleTransitions;
                if (hidden && !value.wasHidden)
                    ++value.hiddenTransitions;
                if (value.wasStale)
                    value.staleMs += delta;
                value.wasStale = stale;
                value.wasHidden = hidden;
                if (measurement.lastValid)
                    value.maxAge = std::max(value.maxAge, now - *measurement.lastValid);
                if (stale || hidden || measurement.quality != Quality::Valid) {
                    finish(ExitCode::Assertion,
                           "Normal observation lost freshness: " + std::string(channelInfo(Channels[i]).name),
                           now);
                    return;
                }
            }
            if (now - observedAt >= config.observationMs) {
                observedUntil = now;
                for (std::size_t i = 0; i < Channels.size(); ++i)
                    scheduleDelays[i] =
                        session.metrics().channels[channelIndex(Channels[i])].scheduleDelay.summary();
                pass(now, "Bounded normal observation: all available channels stayed Valid and visible");
                enter(Phase::Fault, now);
                dlc::Faults fault;
                fault.corruptNext = true;
                session.setFaults(fault);
            }
            break;
        case Phase::Fault:
            if (session.state() == dlc::State::Faulted) {
                if (client.diagnostics().status != hd_bridge::DlcChecksum || !transport.isOpen()) {
                    finish(ExitCode::Assertion, "Expected DLC checksum fault differs from observed fault",
                           now);
                    break;
                }
                ++expectedFaultCount;
                pass(now, "Valid USB frame reported an inner DLC checksum failure");
                enter(Phase::Aging, now);
                intentsAtFault = readIntents;
            }
            break;
        case Phase::Aging: {
            if (!transport.isOpen()) {
                finish(ExitCode::Endpoint, "Transport closed during fault drain", now);
                break;
            }
            if (readIntents != intentsAtFault) {
                finish(ExitCode::Assertion, "Polling continued after DLC fault", now);
                break;
            }
            bool aged = true;
            for (auto channel : Channels)
                aged = aged && session.model().channels()[channelIndex(channel)].quality == Quality::Stale &&
                       !session.model().current(channel, now);
            if (aged) {
                pass(now, "Transport stayed open and drained; all three values became Stale and hidden "
                          "without further reads");
                enter(Phase::Recovery, now);
                session.setFaults({});
                session.setScenario(dlc::Scenario::Baseline);
                session.start(now);
            }
            break;
        }
        case Phase::Recovery:
            if (valuesMatch(now, A)) {
                pass(now,
                     "Explicit NEW_EXPERIMENT restored A through production protocol/parser/decoder/model");
                finish(ExitCode::Success,
                       "All software acceptance scenarios passed; hardware steps remain unverified", now);
            }
            break;
        default:
            break;
        }
    }
};

Runner::Runner(Transport &transport, Config config, Recorder::WriteHook hook)
    : impl_(std::make_unique<Impl>(transport, nullptr, std::move(config), std::move(hook))) {}
Runner::Runner(Transport &transport, Transport &responder, Config config, Recorder::WriteHook hook)
    : impl_(std::make_unique<Impl>(transport, &responder, std::move(config), std::move(hook))) {}
Runner::~Runner() {
    if (impl_->phase != Phase::Done && impl_->phase != Phase::Idle)
        impl_->finish(ExitCode::Cancelled, "Runner destroyed before asynchronous completion", impl_->lastTick,
                      true);
    impl_->session.onRaw = {};
    impl_->session.onSample = {};
    if (impl_->benchController)
        impl_->benchController->onRaw = {};
    else
        impl_->virtualClient->onRaw = {};
}
void Runner::start(Time now) {
    auto &value = *impl_;
    if (value.phase != Phase::Idle)
        return;
    value.started = value.lastTick = value.phaseAt = now;
    if (value.config.reportDirectory.empty() || !value.config.observationMs || !value.config.phaseTimeoutMs ||
        !value.config.overallTimeoutMs ||
        (value.config.backend == Backend::Serial && value.config.port.empty()) ||
        (value.config.backend == Backend::Native && !value.config.port.empty()) ||
        (value.config.backend == Backend::TwoNanoBench &&
         (!value.benchController || value.config.port.empty() ||
          !bench::distinctPorts(value.config.port, value.config.responderPort))) ||
        (value.config.backend != Backend::TwoNanoBench &&
         (value.benchController || !value.config.responderPort.empty()))) {
        value.finish(ExitCode::Arguments,
                     "Explicit backend, report directory and serial-only explicit port required", now);
        return;
    }
    try {
        std::filesystem::create_directories(value.config.reportDirectory);
        // Detect a bad destination before endpoint operations and retire an old
        // PASS immediately: abrupt process termination must leave RUNNING.
        std::ofstream probe(value.config.reportDirectory / "report.json", std::ios::binary | std::ios::trunc);
        probe.exceptions(std::ios::badbit | std::ios::failbit);
        probe << "{\"format_version\":1,\"result\":\"RUNNING\",\"exit_code\":null,\"desktop_sha\":"
              << quote(value.config.desktopSha)
              << ",\"message\":\"Acceptance started; no final result yet\"}\n";
        probe.flush();
        probe.close();
    } catch (const std::exception &error) {
        value.finish(ExitCode::Report, error.what(), now);
        return;
    }
    value.enter(Phase::Handshake, now);
    if (value.benchController)
        value.benchController->connect(now);
    else
        value.virtualClient->connect(now);
}
void Runner::tick(Time now) {
    impl_->tick(now);
}
void Runner::cancel(Time now) {
    impl_->finish(ExitCode::Cancelled, "Acceptance cancelled by caller", now);
}
bool Runner::done() const {
    return impl_->phase == Phase::Done;
}
ExitCode Runner::exitCode() const {
    return impl_->code;
}
const std::string &Runner::message() const {
    return impl_->message;
}
const char *Runner::phaseName() const {
    return name(impl_->phase);
}
std::filesystem::path Runner::reportPath() const {
    return impl_->config.reportDirectory / "report.json";
}
std::filesystem::path Runner::journalDirectory() const {
    return impl_->recorder.directory();
}
const dlc::Session &Runner::session() const {
    return impl_->session;
}
const bridge::Client &Runner::client() const {
    return impl_->client;
}
const bench::Controller *Runner::benchController() const {
    return impl_->benchController.get();
}
bool Runner::flushRecording(std::chrono::milliseconds timeout) {
    return !impl_->recordingStarted || impl_->recorder.waitUntilIdle(timeout);
}
} // namespace hd::acceptance
