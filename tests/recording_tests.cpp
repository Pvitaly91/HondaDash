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
#include <vector>

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

std::vector<std::string> csvCells(const std::string& line) {
    std::vector<std::string> cells(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                cells.back() += '"';
                ++i;
            } else quoted = !quoted;
        } else if (ch == ',' && !quoted) cells.emplace_back();
        else cells.back() += ch;
    }
    require(!quoted, "unbalanced CSV quoting");
    return cells;
}

void partialFormatAndProvenance() {
    TemporaryDirectory temp;
    LocaleGuard locale;
    hd::RecordingMetadata metadata;
    metadata.scenario = "raw-a";
    metadata.transport = "offline-dlc";
    metadata.endpoint = "scripted-honda-ecu";
    metadata.firmware.clear();
    metadata.wireProtocol = "honda-dlc";
    metadata.profile = "honda-dlc-reference-v1";
    metadata.evidenceStatus = "reference-derived; hardware-unverified";
    metadata.fixtureClass = "reference-derived";
    metadata.fixtureId = "raw-a";
    metadata.formatVersion = 3;
    hd::Recorder recorder;
    require(recorder.start(temp.path, metadata), "version 3 start failed");
    hd::Sample sample;
    sample.session = 7;
    sample.request = 1;
    sample.time = 100;
    sample.updatedMask = 4;
    sample.source = metadata.profile;
    sample.rangePolicy = hd::RangePolicy::DecoderValidated;
    sample.values[2] = 155;
    sample.qualities[2] = hd::Quality::Valid;
    sample.reasons[2] = "reference, \"formula\"";
    require(recorder.enqueueSample(sample), "partial coolant enqueue failed");
    require(recorder.enqueueRaw({101, 7, 2, "RX", "checksum rejected", {0x00, 0x04, 0x20, 0x00}}),
            "damaged DLC bytes enqueue failed");
    sample.time = 1500;
    sample.request = 2;
    sample.updatedMask = 1;
    sample.values[0] = 0;
    sample.qualities[0] = hd::Quality::Valid;
    require(recorder.enqueueSample(sample), "partial zero enqueue failed");
    sample.time = 1600;
    sample.request = 3;
    sample.values[0].reset();
    sample.qualities[0] = hd::Quality::Invalid;
    sample.reasons[0] = "undefined conversion";
    require(recorder.enqueueSample(sample), "partial invalid enqueue failed");
    sample.time = 1700;
    sample.session = 8;
    sample.request = 1;
    sample.values[0] = 500;
    sample.qualities[0] = hd::Quality::Valid;
    require(recorder.enqueueSample(sample), "new session enqueue failed");
    recorder.stop();
    require(recorder.error().empty(), "partial recording failed");
    const auto csv = readFile(recorder.directory() / "measurements.csv");
    const auto raw = readFile(recorder.directory() / "raw.jsonl");
    require(raw.find("\"format_version\":3") != std::string::npos &&
            raw.find("\"wire_protocol\":\"honda-dlc\"") != std::string::npos &&
            raw.find("\"profile_version\":1") != std::string::npos &&
            raw.find("\"fixture_class\":\"reference-derived\"") != std::string::npos &&
            raw.find("\"fixture_id\":\"raw-a\"") != std::string::npos &&
            raw.find("\"source\":\"simulation\"") != std::string::npos &&
            raw.find("\"hardware_verified\":false,\"live_enabled\":false") != std::string::npos &&
            raw.find("\"transaction_id_origin\":\"host_only_not_ecu_wire\"") != std::string::npos,
            "version 3 metadata missing or incorrectly claims hardware evidence");
    require(raw.find("\"time_ms\":101,\"host_session_id\":7,\"host_transaction_id\":2,\"kind\":\"RX\"") != std::string::npos &&
            raw.find("\"bytes_hex\":\"00042000\"") != std::string::npos,
            "raw fragment or host transaction origin lost");
    require(raw.find("\"coolant\":{\"updated\":false,\"value\":null,\"quality\":\"Stale\",\"last_valid_ms\":100,\"age_ms\":1400") != std::string::npos,
            "JSON repeated stale coolant as a fresh measurement");
    require(raw.find("\"rpm\":{\"updated\":true,\"value\":0,\"quality\":\"Valid\",\"last_valid_ms\":1500,\"age_ms\":0") != std::string::npos,
            "JSON lost valid zero or partial timestamp");
    require(raw.find("reference, \\\"formula\\\"") != std::string::npos, "formula provenance JSON escaping failed");

    std::istringstream rows(csv);
    std::string line;
    std::getline(rows, line); // Metadata.
    std::getline(rows, line);
    const auto header = csvCells(line);
    require(header.size() == 67 && header[2] == "host_transaction_id" && header[3] == "updated_mask",
            "partial CSV header incorrect");
    std::getline(rows, line);
    const auto coolant = csvCells(line);
    require(coolant[3] == "4" && coolant[22] == "155" && coolant[24] == "1" && coolant[25] == "100" &&
            coolant[30] == "reference, \"formula\"", "partial coolant CSV or quoting failed");
    std::getline(rows, line);
    const auto rpm = csvCells(line);
    require(rpm.size() == 67 && rpm[4] == "0" && rpm[6] == "1" && rpm[7] == "1500" && rpm[8] == "0",
            "partial RPM CSV invalid");
    require(rpm[22].empty() && rpm[23] == "Stale" && rpm[24] == "0" && rpm[25] == "100" &&
            rpm[26] == "1400" && rpm[29] == "1", "RPM refreshed or repeated unrelated coolant");
    std::getline(rows, line);
    const auto invalid = csvCells(line);
    require(invalid[4].empty() && invalid[5] == "Invalid" && invalid[7] == "1500" && invalid[8] == "100",
            "invalid conversion renewed last-valid time");
    std::getline(rows, line);
    const auto restarted = csvCells(line);
    require(restarted[22].empty() && restarted[23] == "NoData" && restarted[25].empty() && restarted[26].empty(),
            "new session inherited old recording provenance");

    metadata.formatVersion = 2;
    require(!recorder.start(temp.path, metadata), "DLC profile silently accepted by snapshot-only version 2");
    require(recorder.start(temp.path, {"manual", 1}), "version 2 restart failed");
    require(!recorder.enqueueSample(sample), "partial sample silently accepted in version 2");
    recorder.stop();
    require(!recorder.error().empty(), "version 2 incompatible sample rejection was invisible");
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
        partialFormatAndProvenance();
        visibleFailures();
        boundedQueue();
        std::cout << "recording: v2/v3 CSV/JSONL, partial provenance/age, Unicode paths, lifecycle, failures and bounds passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "recording test failed: " << exception.what() << '\n';
        return 1;
    }
}
