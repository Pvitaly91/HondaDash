#include "recording/recorder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <variant>

namespace hd {
namespace {
constexpr std::array<const char*, ChannelCount> Names{
    "rpm", "speed", "coolant", "intake", "throttle", "map", "voltage"};
constexpr std::array<const char*, ChannelCount> Units{
    "rpm", "km/h", "degC", "degC", "%", "kPa", "V"};

std::string jsonString(const std::string& value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch) << std::dec;
            else out << static_cast<char>(ch);
        }
    }
    return out.str() + '"';
}

std::string metadataJson(const RecordingMetadata& metadata) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto wallTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    out << "{\"kind\":\"metadata\",\"format_version\":" << metadata.formatVersion
        << ",\"app_version\":\"0.6.0\",\"source\":\"simulation\",\"profile\":"
        << jsonString(metadata.profile) << ",\"scenario\":"
        << jsonString(metadata.scenario) << ",\"seed\":" << metadata.seed
        << ",\"transport\":" << jsonString(metadata.transport) << ",\"endpoint\":" << jsonString(metadata.endpoint)
        << ",\"firmware\":" << jsonString(metadata.firmware) << ",\"port\":" << jsonString(metadata.port)
        << ",\"baud\":" << metadata.baud;
    if (metadata.formatVersion == 3) {
        out << ",\"wire_protocol\":" << jsonString(metadata.wireProtocol)
            << ",\"profile_version\":" << metadata.profileVersion
            << ",\"evidence_status\":" << jsonString(metadata.evidenceStatus)
            << ",\"fixture_class\":" << jsonString(metadata.fixtureClass)
            << ",\"fixture_id\":" << jsonString(metadata.fixtureId)
            << ",\"hardware_verified\":" << (metadata.hardwareVerified ? "true" : "false")
            << ",\"live_enabled\":" << (metadata.liveEnabled ? "true" : "false")
            << ",\"transaction_id_origin\":\"host_only_not_ecu_wire\""
            << ",\"stale_after_ms\":" << metadata.freshness.staleMs
            << ",\"hide_after_ms\":" << metadata.freshness.hideMs
            << ",\"freshness_boundary\":\"age_gt_threshold\""
            << ",\"freshness_by_channel\":{";
        for (std::size_t i = 0; i < ChannelCount; ++i) {
            if (i) out << ',';
            const auto thresholds = metadata.freshness.effective(static_cast<Channel>(i));
            out << jsonString(Names[i]) << ":{\"stale_after_ms\":" << thresholds.staleMs
                << ",\"hide_after_ms\":" << thresholds.hideMs << '}';
        }
        out << '}';
        if (!metadata.schedulerPolicy.empty() || !metadata.freshnessPolicy.empty()) {
            out << ",\"scheduler_policy\":" << jsonString(metadata.schedulerPolicy)
                << ",\"scheduler_policy_version\":" << metadata.schedulerPolicyVersion
                << ",\"freshness_policy\":" << jsonString(metadata.freshnessPolicy)
                << ",\"freshness_policy_version\":" << metadata.freshnessPolicyVersion
                << ",\"timing_estimate_source\":" << jsonString(metadata.timingEstimateSource)
                << ",\"measurement_scope\":" << jsonString(metadata.measurementScope)
                << ",\"requested_intervals_ms\":{";
            for (std::size_t i = 0; i < ChannelCount; ++i) {
                if (i) out << ',';
                out << jsonString(Names[i]) << ':' << metadata.requestedIntervals[i];
            }
            out << '}';
        }
        if (!metadata.outerProtocol.empty()) {
            out << ",\"outer_protocol\":" << jsonString(metadata.outerProtocol)
                << ",\"bridge_identity\":" << jsonString(metadata.bridgeIdentity)
                << ",\"bridge_version\":" << metadata.bridgeVersion
                << ",\"backend\":" << jsonString(metadata.backend)
                << ",\"read_policy_version\":" << metadata.readPolicyVersion
                << ",\"freshness_time_policy\":\"host_request_start_lower_bound\"";
            if (metadata.benchSchemaVersion) {
                out << ",\"bench_schema_version\":" << metadata.benchSchemaVersion
                    << ",\"bench_io_enabled\":" << (metadata.benchIoEnabled ? "true" : "false")
                    << ",\"vehicle_connection_allowed\":" << (metadata.vehicleConnectionAllowed ? "true" : "false")
                    << ",\"responder_identity\":" << jsonString(metadata.responderIdentity)
                    << ",\"responder_firmware\":" << jsonString(metadata.responderFirmware)
                    << ",\"responder_protocol_version\":" << metadata.responderProtocolVersion
                    << ",\"responder_read_policy_version\":" << metadata.responderReadPolicyVersion
                    << ",\"responder_port\":" << jsonString(metadata.responderPort)
                    << ",\"firmware_target\":" << jsonString(metadata.firmwareTarget);
            } else out << ",\"physical_dlc_enabled\":" << (metadata.physicalDlcEnabled ? "true" : "false");
        }
    }
    out << ",\"created_unix_ms\":" << wallTime << ",\"clock\":\"monotonic_ms\",\"units\":{";
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        if (i) out << ',';
        out << jsonString(Names[i]) << ':' << jsonString(Units[i]);
    }
    return out.str() + "}}";
}

std::string rawJson(const RawEvent& event, std::uint32_t version) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"time_ms\":" << event.time
        << (version == 3 ? ",\"host_session_id\":" : ",\"session_id\":") << event.session
        << (version == 3 ? ",\"host_transaction_id\":" : ",\"request_id\":") << event.request
        << ",\"kind\":" << jsonString(event.kind)
        << ",\"detail\":" << jsonString(event.detail) << ",\"bytes_hex\":\"";
    for (const auto byte : event.bytes)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    out << '"' << std::dec;
    if (version == 3 && event.bridge) {
        const auto& bridge = *event.bridge;
        out << ",\"bridge\":{\"origin\":" << jsonString(bridge.origin)
            << ",\"generation\":" << bridge.generation << ",\"operation\":" << bridge.operation
            << ",\"sequence\":" << bridge.sequence << ",\"tx_elapsed_ms\":" << bridge.txElapsedMs
            << ",\"rx_elapsed_ms\":" << bridge.rxElapsedMs << ",\"max_gap_ms\":" << bridge.maxGapMs
            << ",\"status\":" << unsigned(bridge.status) << '}';
    }
    return out.str() + '}';
}

std::string sampleCsv(const Sample& sample) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(12) << sample.time << ',' << sample.session << ',' << sample.request;
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        out << ',';
        if (sample.values[i]) out << *sample.values[i];
        out << ',' << qualityName(sample.qualities[i]);
    }
    return out.str();
}

std::string csvString(const std::string& value) {
    std::string result{"\""};
    for (const auto ch : value) {
        if (ch == '"') result += '"';
        result += ch;
    }
    return result + '"';
}

Time measurementAge(Time now, Time then) { return now >= then ? now - then : 0; }

std::string partialCsv(const Sample& sample, const Model& state) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(12) << sample.time << ',' << sample.session << ',' << sample.request
        << ',' << unsigned(sample.updatedMask & AllChannelsMask);
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        const bool updated = (sample.updatedMask & (1u << i)) != 0;
        const auto& measurement = state.channels()[i];
        out << ',';
        // The timestamp belongs only to changed channels. Historical values are
        // never repeated as fresh CSV measurements in a different block's row.
        if (updated && measurement.value &&
            (measurement.quality == Quality::Valid || measurement.quality == Quality::Stale))
            out << *measurement.value;
        out << ',' << qualityName(measurement.quality) << ',' << (updated ? 1 : 0) << ',';
        if (measurement.lastValid) out << *measurement.lastValid;
        out << ',';
        if (measurement.lastValid) out << measurementAge(sample.time, *measurement.lastValid);
        out << ',' << csvString(measurement.source) << ',' << measurement.session << ',' << measurement.request
            << ',' << csvString(measurement.reason);
    }
    return out.str();
}

std::string partialJson(const Sample& sample, const Model& state) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(12) << "{\"kind\":\"partial_sample\",\"time_ms\":" << sample.time
        << ",\"host_session_id\":" << sample.session << ",\"host_transaction_id\":" << sample.request
        << ",\"updated_mask\":" << unsigned(sample.updatedMask & AllChannelsMask)
        << ",\"profile\":" << jsonString(sample.source);
    if (sample.freshnessSince)
        out << ",\"freshness_lower_bound_ms\":" << std::min(sample.time, *sample.freshnessSince);
    out << ",\"channels\":{";
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        if (i) out << ',';
        const bool updated = (sample.updatedMask & (1u << i)) != 0;
        const auto& measurement = state.channels()[i];
        out << jsonString(Names[i]) << ":{\"updated\":" << (updated ? "true" : "false") << ",\"value\":";
        if (updated && measurement.value &&
            (measurement.quality == Quality::Valid || measurement.quality == Quality::Stale)) out << *measurement.value;
        else out << "null";
        out << ",\"quality\":" << jsonString(qualityName(measurement.quality)) << ",\"last_valid_ms\":";
        if (measurement.lastValid) out << *measurement.lastValid;
        else out << "null";
        out << ",\"age_ms\":";
        if (measurement.lastValid) out << measurementAge(sample.time, *measurement.lastValid);
        else out << "null";
        out << ",\"source\":" << jsonString(measurement.source)
            << ",\"host_session_id\":" << measurement.session
            << ",\"host_transaction_id\":" << measurement.request
            << ",\"reason\":" << jsonString(measurement.reason) << '}';
    }
    return out.str() + "}}";
}
} // namespace

struct Recorder::Impl {
    using Record = std::variant<RawEvent, Sample>;
    explicit Impl(std::size_t size, WriteHook hook) : capacity(std::max<std::size_t>(1, size)), beforeWrite(std::move(hook)) {}
    const std::size_t capacity;
    WriteHook beforeWrite;
    mutable std::mutex mutex;
    std::condition_variable wake, drained;
    std::deque<Record> queue;
    std::thread worker;
    std::filesystem::path directory;
    std::string error;
    std::uint32_t formatVersion{2};
    bool accepting{}, stopping{}, ready{}, workerRunning{}, inFlight{};

    void fail(std::string message, bool discard) {
        std::lock_guard lock(mutex);
        if (error.empty()) error = std::move(message);
        accepting = false;
        stopping = true;
        if (discard) queue.clear();
        wake.notify_all();
        drained.notify_all();
    }

    bool enqueue(Record record) {
        std::lock_guard lock(mutex);
        if (!accepting) return false;
        if (const auto* event = std::get_if<RawEvent>(&record); event && event->bridge && formatVersion != 3) {
            error = "Дворівневий bridge trace потребує format_version=3; запис зупинено.";
            accepting = false;
            stopping = true;
            wake.notify_all();
            drained.notify_all();
            return false;
        }
        if (const auto* sample = std::get_if<Sample>(&record);
            sample && formatVersion == 2 &&
            (sample->updatedMask != AllChannelsMask || sample->source != "synthetic-demo-v1" || sample->freshnessSince)) {
            error = "Часткові вимірювання та інший профіль потребують format_version=3; запис зупинено.";
            accepting = false;
            stopping = true;
            wake.notify_all();
            drained.notify_all();
            return false;
        }
        if (queue.size() >= capacity) {
            error = "Переповнення черги запису (" + std::to_string(capacity) + "); запис зупинено.";
            accepting = false;
            stopping = true;
            wake.notify_all();
            drained.notify_all();
            return false;
        }
        queue.push_back(std::move(record));
        wake.notify_one();
        return true;
    }

    void run(std::filesystem::path parent, RecordingMetadata metadata) {
        try {
            std::filesystem::create_directories(parent);
            const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::filesystem::path target;
            for (unsigned attempt = 0; attempt != 1000; ++attempt) {
                auto candidate = parent / ("recording-" + std::to_string(epoch) + '-' + std::to_string(attempt));
                if (std::filesystem::create_directory(candidate)) {
                    target = std::move(candidate);
                    break;
                }
            }
            if (target.empty()) throw std::runtime_error("recording directory collision limit exceeded");
            std::ofstream csv(target / "measurements.csv", std::ios::binary);
            std::ofstream raw(target / "raw.jsonl", std::ios::binary);
            csv.imbue(std::locale::classic());
            raw.imbue(std::locale::classic());
            csv.exceptions(std::ios::badbit | std::ios::failbit);
            raw.exceptions(std::ios::badbit | std::ios::failbit);
            const auto metadataLine = metadataJson(metadata);
            csv << "# " << metadataLine << '\n';
            if (metadata.formatVersion == 3) {
                csv << "time_ms,host_session_id,host_transaction_id,updated_mask";
                for (const auto name : Names) {
                    csv << ',' << name << ',' << name << "_state," << name << "_updated," << name
                        << "_last_valid_ms," << name << "_age_ms," << name << "_source," << name
                        << "_host_session_id," << name << "_host_transaction_id," << name << "_reason";
                }
            } else {
                csv << "time_ms,session_id,request_id";
                for (const auto name : Names) csv << ',' << name << ',' << name << "_state";
            }
            csv << '\n';
            raw << metadataLine << '\n';
            csv.flush();
            raw.flush();
            {
                std::lock_guard lock(mutex);
                directory = target;
                ready = true;
                inFlight = false;
                drained.notify_all();
            }
            Model state(metadata.freshness);
            std::optional<std::uint32_t> sampleSession;
            std::string sampleProfile;
            while (true) {
                Record record;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return stopping || !queue.empty(); });
                    if (queue.empty()) break;
                    record = std::move(queue.front());
                    queue.pop_front();
                    inFlight = true;
                }
                if (beforeWrite && !beforeWrite()) throw std::runtime_error("injected write failure");
                if (const auto* event = std::get_if<RawEvent>(&record)) {
                    raw << rawJson(*event, metadata.formatVersion) << '\n';
                    raw.flush();
                } else {
                    const auto& sample = std::get<Sample>(record);
                    if (metadata.formatVersion == 3) {
                        if (!sampleSession || *sampleSession != sample.session || sampleProfile != sample.source) {
                            state.reset();
                            sampleSession = sample.session;
                            sampleProfile = sample.source;
                        }
                        state.apply(sample);
                        state.refresh(sample.time);
                        csv << partialCsv(sample, state) << '\n';
                        raw << partialJson(sample, state) << '\n';
                        raw.flush();
                    } else csv << sampleCsv(sample) << '\n';
                    csv.flush();
                }
                {
                    std::lock_guard lock(mutex);
                    inFlight = false;
                    drained.notify_all();
                }
            }
            csv.flush();
            raw.flush();
            csv.close();
            raw.close();
        } catch (const std::exception& exception) {
            fail("Не вдалося записати журнал: " + std::string(exception.what()), true);
        } catch (...) {
            fail("Не вдалося записати журнал: невідома помилка.", true);
        }
        std::lock_guard lock(mutex);
        accepting = false;
        workerRunning = false;
        inFlight = false;
        drained.notify_all();
    }
};

Recorder::Recorder(std::size_t capacity, WriteHook beforeWrite)
    : impl_(std::make_unique<Impl>(capacity, std::move(beforeWrite))) {}
Recorder::~Recorder() { stop(); }

bool Recorder::start(std::filesystem::path directory, RecordingMetadata metadata) {
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->workerRunning) return false;
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard lock(impl_->mutex);
    impl_->error.clear();
    if ((metadata.formatVersion != 2 && metadata.formatVersion != 3) ||
        (metadata.formatVersion == 2 && (metadata.profile != "synthetic-demo-v1" ||
                                        metadata.wireProtocol != "synthetic-demo-v1" ||
                                        !metadata.schedulerPolicy.empty() || !metadata.freshnessPolicy.empty() ||
                                        !metadata.timingEstimateSource.empty() || !metadata.measurementScope.empty() ||
                                        metadata.schedulerPolicyVersion || metadata.freshnessPolicyVersion ||
                                        metadata.benchSchemaVersion || metadata.benchIoEnabled ||
                                        metadata.vehicleConnectionAllowed || !metadata.responderIdentity.empty() ||
                                        !metadata.responderFirmware.empty() || !metadata.responderPort.empty() ||
                                        !metadata.firmwareTarget.empty() || metadata.responderProtocolVersion ||
                                        metadata.responderReadPolicyVersion ||
                                        std::any_of(metadata.requestedIntervals.begin(), metadata.requestedIntervals.end(),
                                                    [](Time value) { return value != 0; }) ||
                                        std::any_of(metadata.freshness.channels.begin(), metadata.freshness.channels.end(),
                                                    [](const auto& value) { return value.has_value(); }))) ||
        !metadata.freshness.valid() || metadata.vehicleConnectionAllowed ||
        (metadata.benchSchemaVersion && (metadata.benchSchemaVersion != 1 || !metadata.benchIoEnabled ||
                                        metadata.outerProtocol.empty())) ||
        (metadata.benchIoEnabled && !metadata.benchSchemaVersion)) {
        impl_->error = "Непідтримувана версія або несумісні метадані журналу.";
        return false;
    }
    if (directory.empty()) {
        impl_->error = "Папку для запису не вибрано.";
        return false;
    }
    impl_->directory = directory;
    impl_->queue.clear();
    impl_->formatVersion = metadata.formatVersion;
    impl_->accepting = true;
    impl_->stopping = false;
    impl_->ready = false;
    impl_->workerRunning = true;
    impl_->inFlight = true;
    try {
        impl_->worker = std::thread([this, directory = std::move(directory), metadata = std::move(metadata)]() mutable {
            impl_->run(std::move(directory), std::move(metadata));
        });
    } catch (const std::exception& exception) {
        impl_->error = "Не вдалося почати запис: " + std::string(exception.what());
        impl_->accepting = impl_->workerRunning = false;
        impl_->inFlight = false;
        return false;
    }
    return true;
}

void Recorder::stop() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->accepting = false;
        impl_->stopping = true;
        impl_->wake.notify_all();
    }
    if (impl_->worker.joinable()) impl_->worker.join();
}
bool Recorder::waitUntilIdle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(impl_->mutex);
    const auto settled = impl_->drained.wait_for(lock, timeout, [this] {
        return (!impl_->inFlight && impl_->queue.empty()) || !impl_->error.empty() || !impl_->workerRunning;
    });
    return settled && impl_->error.empty() && impl_->queue.empty() && !impl_->inFlight;
}
bool Recorder::enqueueRaw(const RawEvent& event) { return impl_->enqueue(event); }
bool Recorder::enqueueSample(const Sample& sample) { return impl_->enqueue(sample); }
std::string Recorder::status() const {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->error.empty()) return "Помилка запису";
    if (impl_->accepting) return impl_->ready ? "Запис триває" : "Відкриття журналу";
    return impl_->workerRunning ? "Завершення запису" : "Запис зупинено";
}
std::string Recorder::error() const { std::lock_guard lock(impl_->mutex); return impl_->error; }
bool Recorder::active() const { std::lock_guard lock(impl_->mutex); return impl_->accepting; }
std::filesystem::path Recorder::directory() const { std::lock_guard lock(impl_->mutex); return impl_->directory; }

} // namespace hd
