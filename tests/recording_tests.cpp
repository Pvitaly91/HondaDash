#include "recording/recorder.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "recording output missing");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            std::filesystem::path(u8"HondaDash тести з пробілами") / std::to_string(ticks);
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
struct CommaDecimal : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
struct LocaleGuard {
    std::locale previous{std::locale()};
    LocaleGuard() { std::locale::global(std::locale(std::locale::classic(), new CommaDecimal)); }
    ~LocaleGuard() { std::locale::global(previous); }
};

void outputAndLifecycle() {
    TemporaryDirectory temp;
    LocaleGuard locale;
    hd::Recorder recorder;
    require(recorder.start(temp.path, {"manual\"\nscenario", 42}), "start was not accepted");
    require(!recorder.start(temp.path, {"other", 0}), "duplicate active start accepted");
    hd::Sample sample;
    sample.time = 1250;
    sample.session = 7;
    sample.request = 19;
    sample.values = {0.0, std::nullopt, -20.5, std::nullopt, 12.5, 101.2, 13.8};
    sample.qualities = {hd::Quality::Valid, hd::Quality::Unsupported, hd::Quality::Valid,
                       hd::Quality::Invalid, hd::Quality::Valid, hd::Quality::Valid, hd::Quality::Valid};
    require(recorder.enqueueSample(sample), "sample enqueue failed");
    require(recorder.enqueueRaw({1250, 7, 19, "RX", "corrupt\"\nfragment", {0xa5, 0x00, 0xff}}), "raw enqueue failed");
    recorder.stop();
    require(!recorder.active() && recorder.error().empty(), "stop or flush failed");
    require(!recorder.enqueueSample(sample), "stopped recorder accepted a sample");
    const auto firstDirectory = recorder.directory();
    const auto csv = readFile(firstDirectory / "measurements.csv");
    const auto raw = readFile(firstDirectory / "raw.jsonl");
    require(csv.find("1250,7,19,0,Valid,,Unsupported,-20.5,Valid,,Invalid,12.5,Valid,101.2,Valid,13.8,Valid\n") != std::string::npos,
            "CSV optional values, zero, negative temperature or locale failed");
    require(csv.find("\"format_version\":2") != std::string::npos && csv.find("\"source\":\"simulation\"") != std::string::npos,
            "CSV metadata missing");
    require(raw.find("\"profile\":\"synthetic-demo-v1\"") != std::string::npos &&
            raw.find("\"seed\":42") != std::string::npos && raw.find("\"voltage\":\"V\"") != std::string::npos,
            "raw metadata missing");
    require(raw.find("manual\\\"\\nscenario") != std::string::npos && raw.find("corrupt\\\"\\nfragment") != std::string::npos,
            "JSON escaping failed");
    require(raw.find("\"time_ms\":1250,\"session_id\":7,\"request_id\":19") != std::string::npos &&
            raw.find("\"bytes_hex\":\"a500ff\"") != std::string::npos,
            "raw corrupt fragment or identities lost");
    for (unsigned iteration = 0; iteration != 20; ++iteration) {
        require(recorder.start(temp.path, {"idle", iteration}), "restart failed");
        require(recorder.enqueueRaw({iteration, iteration + 1, 0, "START", {}, {}}), "restart enqueue failed");
        recorder.stop();
        require(recorder.error().empty(), "restart produced an error");
        require(recorder.directory() != firstDirectory, "recording overwrote a prior directory");
    }
    recorder.stop();
}

void visibleFailures() {
    TemporaryDirectory temp;
    hd::Recorder recorder;
    require(!recorder.start({}, {"demo", 1}) && !recorder.error().empty(), "empty path error missing");
    const auto notDirectory = temp.path / "a regular file";
    { std::ofstream file(notDirectory); file << "occupied"; }
    require(recorder.start(notDirectory, {"demo", 1}), "async invalid path start should be accepted");
    recorder.stop();
    require(!recorder.active() && !recorder.error().empty(), "directory/open failure was not visible");
    hd::Recorder failing(8, [] { return false; });
    require(failing.start(temp.path, {"demo", 1}), "write failure start failed");
    require(failing.enqueueRaw({1, 1, 1, "TX", {}, {1}}), "write failure enqueue failed");
    failing.stop();
    require(failing.error().find("injected write failure") != std::string::npos && !failing.active(), "write failure was not surfaced");
}

void boundedQueue() {
    TemporaryDirectory temp;
    std::mutex mutex;
    std::condition_variable wake;
    bool entered = false, release = false;
    hd::Recorder recorder(1, [&] {
        std::unique_lock lock(mutex);
        entered = true;
        wake.notify_one();
        wake.wait(lock, [&] { return release; });
        return true;
    });
    require(recorder.start(temp.path, {"idle", 3}), "queue test start failed");
    require(recorder.enqueueRaw({1, 1, 1, "RX", {}, {1}}), "first queue enqueue failed");
    {
        std::unique_lock lock(mutex);
        const auto observed = wake.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
        if (!observed) { release = true; wake.notify_all(); }
        require(observed, "worker did not reach the test gate");
    }
    const auto second = recorder.enqueueRaw({2, 1, 2, "RX", {}, {2}});
    const auto overflow = recorder.enqueueRaw({3, 1, 3, "RX", {}, {3}});
    {
        std::lock_guard lock(mutex);
        release = true;
        wake.notify_all();
    }
    recorder.stop();
    require(second && !overflow, "bounded queue did not reject overflowing record");
    require(!recorder.error().empty() && !recorder.active(), "queue overflow was not visible");
    const auto raw = readFile(recorder.directory() / "raw.jsonl");
    require(raw.find("\"bytes_hex\":\"01\"") != std::string::npos && raw.find("\"bytes_hex\":\"02\"") != std::string::npos &&
            raw.find("\"bytes_hex\":\"03\"") == std::string::npos, "accepted queue records were not drained faithfully");
}
}

int main() {
    try {
        outputAndLifecycle();
        visibleFailures();
        boundedQueue();
        std::cout << "recording: CSV/JSONL, Unicode paths, lifecycle, write failures and bounded queue passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "recording test failed: " << exception.what() << '\n';
        return 1;
    }
}
