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
    out << "{\"kind\":\"metadata\",\"format_version\":1,\"app_version\":\"0.1.0\","
           "\"source\":\"simulation\",\"profile\":\"synthetic-demo-v1\",\"scenario\":"
        << jsonString(metadata.scenario) << ",\"seed\":" << metadata.seed
        << ",\"created_unix_ms\":" << wallTime << ",\"clock\":\"monotonic_ms\",\"units\":{";
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        if (i) out << ',';
        out << jsonString(Names[i]) << ':' << jsonString(Units[i]);
    }
    return out.str() + "}}";
}

std::string rawJson(const RawEvent& event) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"time_ms\":" << event.time << ",\"session_id\":" << event.session
        << ",\"request_id\":" << event.request << ",\"kind\":" << jsonString(event.kind)
        << ",\"detail\":" << jsonString(event.detail) << ",\"bytes_hex\":\"";
    for (const auto byte : event.bytes)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return out.str() + "\"}";
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
} // namespace

struct Recorder::Impl {
    using Record = std::variant<RawEvent, Sample>;
    explicit Impl(std::size_t size, WriteHook hook) : capacity(std::max<std::size_t>(1, size)), beforeWrite(std::move(hook)) {}
    const std::size_t capacity;
    WriteHook beforeWrite;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Record> queue;
    std::thread worker;
    std::filesystem::path directory;
    std::string error;
    bool accepting{}, stopping{}, ready{}, workerRunning{};

    void fail(std::string message, bool discard) {
        std::lock_guard lock(mutex);
        if (error.empty()) error = std::move(message);
        accepting = false;
        stopping = true;
        if (discard) queue.clear();
        wake.notify_all();
    }

    bool enqueue(Record record) {
        std::lock_guard lock(mutex);
        if (!accepting) return false;
        if (queue.size() >= capacity) {
            error = "Переповнення черги запису (" + std::to_string(capacity) + "); запис зупинено.";
            accepting = false;
            stopping = true;
            wake.notify_all();
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
            csv << "# " << metadataLine << '\n' << "time_ms,session_id,request_id";
            for (const auto name : Names) csv << ',' << name << ',' << name << "_state";
            csv << '\n';
            raw << metadataLine << '\n';
            csv.flush();
            raw.flush();
            {
                std::lock_guard lock(mutex);
                directory = target;
                ready = true;
            }
            while (true) {
                Record record;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return stopping || !queue.empty(); });
                    if (queue.empty()) break;
                    record = std::move(queue.front());
                    queue.pop_front();
                }
                if (beforeWrite && !beforeWrite()) throw std::runtime_error("injected write failure");
                if (const auto* event = std::get_if<RawEvent>(&record)) {
                    raw << rawJson(*event) << '\n';
                    raw.flush();
                } else {
                    csv << sampleCsv(std::get<Sample>(record)) << '\n';
                    csv.flush();
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
    if (directory.empty()) {
        impl_->error = "Папку для запису не вибрано.";
        return false;
    }
    impl_->directory = directory;
    impl_->queue.clear();
    impl_->accepting = true;
    impl_->stopping = false;
    impl_->ready = false;
    impl_->workerRunning = true;
    try {
        impl_->worker = std::thread([this, directory = std::move(directory), metadata = std::move(metadata)]() mutable {
            impl_->run(std::move(directory), std::move(metadata));
        });
    } catch (const std::exception& exception) {
        impl_->error = "Не вдалося почати запис: " + std::string(exception.what());
        impl_->accepting = impl_->workerRunning = false;
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
