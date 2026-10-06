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

void bridgeTraceMetadata() {
    TemporaryDirectory temp;
    hd::Recorder recorder;
    hd::RecordingMetadata metadata{"raw-a", 0};
    metadata.formatVersion = 3;
    metadata.wireProtocol = "honda-dlc";
    metadata.profile = "honda-dlc-kerpz-obd1-reference-v1";
    metadata.outerProtocol = "hondadash-dlc-bridge-lab-v1";
    metadata.bridgeIdentity = metadata.outerProtocol;
    metadata.bridgeVersion = 1;
    metadata.readPolicyVersion = 1;
    metadata.backend = "virtual";
    metadata.transport = "in-memory-embedded-bridge";
    require(recorder.start(temp.path, metadata), "bridge recording failed to start");
    hd::RawEvent corrupted{200, 7, 3, "usb_rx", "outer checksum rejected", {0xa5,0x5a,0x00}};
    require(recorder.enqueueRaw(corrupted), "outer corruption enqueue failed");
    hd::RawEvent inner{300, 7, 3, "bridge_dlc_rx", "bad inner checksum under trusted outer frame", {0x00,0x04,0x20,0x00}};
    inner.bridge = hd::BridgeTrace{0xfedcba98u, 3, 10, 6, 200, 2, 4, "bridge_reported"};
    require(recorder.enqueueRaw(inner), "bridge trace enqueue failed");
    hd::Sample delayed;
    delayed.time = 2101;
    delayed.freshnessSince = 1000;
    delayed.session = 7;
    delayed.request = 4;
    delayed.source = metadata.profile;
    delayed.updatedMask = 1;
    delayed.values[0] = 750;
    delayed.qualities[0] = hd::Quality::Valid;
    require(recorder.enqueueSample(delayed), "delayed bridge sample enqueue failed");
    recorder.stop();
    require(recorder.error().empty(), "bridge recording error");
    const auto raw = readFile(recorder.directory() / "raw.jsonl");
    require(raw.find("\"outer_protocol\":\"hondadash-dlc-bridge-lab-v1\"") != std::string::npos &&
            raw.find("\"backend\":\"virtual\"") != std::string::npos &&
            raw.find("\"physical_dlc_enabled\":false") != std::string::npos &&
            raw.find("\"read_policy_version\":1") != std::string::npos,
            "bridge metadata absent");
    const auto corruptStart = raw.find("\"kind\":\"usb_rx\"");
    const auto corruptEnd = raw.find('\n', corruptStart);
    require(raw.substr(corruptStart, corruptEnd-corruptStart).find("\"bridge\"") == std::string::npos,
            "outer corruption fabricated trusted inner facts");
    require(raw.find("\"generation\":4275878552,\"operation\":3,\"sequence\":10,\"tx_elapsed_ms\":6,\"rx_elapsed_ms\":200,\"max_gap_ms\":2,\"status\":4") != std::string::npos,
            "bridge relative time/generation lost or written in hex");
    require(raw.find("\"bytes_hex\":\"00042000\"") != std::string::npos,
            "bad inner bytes were changed");
    require(raw.find("\"time_ms\":2101,\"host_session_id\":7,\"host_transaction_id\":4") != std::string::npos &&
            raw.find("\"freshness_lower_bound_ms\":1000") != std::string::npos &&
            raw.find("\"value\":750,\"quality\":\"Stale\",\"last_valid_ms\":1000,\"age_ms\":1101") != std::string::npos,
            "receipt time or conservative age lost; delayed sample presented as fresh");
    require(readFile(recorder.directory() / "measurements.csv").find("2101,7,4,1,750,Stale,1,1000,1101") != std::string::npos,
            "CSV lost a valid but already stale bridge measurement");
    require(recorder.start(temp.path, {"manual", 1}), "legacy recording restart failed");
    require(!recorder.enqueueRaw(inner), "bridge metadata silently discarded by version 2");
    recorder.stop();
}

void channelPolicyAndIdle() {
    TemporaryDirectory temp;
    hd::RecordingMetadata metadata{"raw-a", 0};
    metadata.formatVersion = 3;
    metadata.schedulerPolicy = "bridge-slots-v1";
    metadata.freshnessPolicy = "bridge-bounded-v1";
    metadata.schedulerPolicyVersion = metadata.freshnessPolicyVersion = 1;
    metadata.timingEstimateSource = "host_request_start_lower_bound";
    metadata.measurementScope = "virtual_only";
    metadata.requestedIntervals[0] = 800;
    metadata.requestedIntervals[2] = 1600;
    metadata.freshness.channels[0] = hd::FreshnessThresholds{1400, 4200};
    metadata.freshness.channels[2] = hd::FreshnessThresholds{2100, 6300};
    hd::Recorder recorder;
    require(recorder.start(temp.path, metadata), "per-channel metadata start failed");
    require(recorder.waitUntilIdle(std::chrono::seconds(5)), "startup did not become idle");
    hd::Sample sample;
    sample.time = 320;
    sample.freshnessSince = 0;
    sample.updatedMask = 5;
    sample.values[0] = 0;
    sample.values[2] = 61;
    sample.qualities[0] = sample.qualities[2] = hd::Quality::Valid;
    require(recorder.enqueueSample(sample), "policy first sample enqueue failed");
    sample.time = 1400;
    sample.updatedMask = 0;
    require(recorder.enqueueSample(sample), "equality snapshot enqueue failed");
    sample.time = 1401;
    require(recorder.enqueueSample(sample), "past boundary snapshot enqueue failed");
    sample.time = 2101;
    sample.updatedMask = 1;
    sample.freshnessSince = 500;
    require(recorder.enqueueSample(sample), "already stale enqueue failed");
    require(recorder.waitUntilIdle(std::chrono::seconds(5)), "writer did not flush accepted samples");
    const auto raw = readFile(recorder.directory() / "raw.jsonl");
    require(raw.find("\"scheduler_policy\":\"bridge-slots-v1\"") != std::string::npos &&
            raw.find("\"freshness_policy_version\":1") != std::string::npos &&
            raw.find("\"requested_intervals_ms\":{\"rpm\":800,\"speed\":0,\"coolant\":1600") != std::string::npos &&
            raw.find("\"freshness_by_channel\":{\"rpm\":{\"stale_after_ms\":1400,\"hide_after_ms\":4200}") != std::string::npos &&
            raw.find("\"freshness_boundary\":\"age_gt_threshold\"") != std::string::npos,
            "reproducible per-channel policy metadata missing");
    require(raw.find("\"quality\":\"Valid\",\"last_valid_ms\":0,\"age_ms\":1400") != std::string::npos &&
            raw.find("\"quality\":\"Stale\",\"last_valid_ms\":0,\"age_ms\":1401") != std::string::npos &&
            raw.find("\"quality\":\"Valid\",\"last_valid_ms\":0,\"age_ms\":1401") != std::string::npos &&
            raw.find("\"value\":0,\"quality\":\"Stale\",\"last_valid_ms\":500,\"age_ms\":1601") != std::string::npos,
            "recorded channel qualities diverge from model boundaries/lower bound");
    recorder.stop();
    metadata.formatVersion = 2;
    require(!recorder.start(temp.path, metadata), "v2 silently accepted channel policy");
    metadata = {"raw-a", 0};
    metadata.formatVersion = 3;
    metadata.freshness.channels[2] = hd::FreshnessThresholds{200, 100};
    require(!recorder.start(temp.path, metadata), "invalid per-channel thresholds accepted");
}

void benchMetadata() {
    TemporaryDirectory temp;
    hd::Recorder recorder;
    hd::RecordingMetadata metadata{"raw-a",0};
    metadata.formatVersion=3; metadata.outerProtocol="hondadash-dlc-bridge-bench-v1";
    metadata.backend="two-nano-bench"; metadata.benchSchemaVersion=1; metadata.benchIoEnabled=true;
    metadata.responderIdentity="hondadash-dlc-responder-bench-v1"; metadata.responderFirmware="1.0.0";
    metadata.responderProtocolVersion=2; metadata.responderReadPolicyVersion=2;
    metadata.port="COM31"; metadata.responderPort="COM32"; metadata.firmwareTarget="bridge-bench + responder-bench";
    require(recorder.start(temp.path,metadata),"bench metadata rejected");
    recorder.enqueueRaw({1,1,0,"responder_usb_rx","control only",{0x42}});
    recorder.enqueueRaw({2,1,0,"dlc_rx","MCU reported line",{0x04}});
    recorder.stop();
    const auto raw=readFile(recorder.directory()/"raw.jsonl");
    require(raw.find("\"bench_schema_version\":1")!=std::string::npos && raw.find("\"bench_io_enabled\":true")!=std::string::npos && raw.find("\"vehicle_connection_allowed\":false")!=std::string::npos,"bench scope missing");
    require(raw.find("physical_dlc_enabled")==std::string::npos,"bench reused misleading legacy flag");
    require(raw.find("\"responder_port\":\"COM32\"")!=std::string::npos && raw.find("\"responder_protocol_version\":2")!=std::string::npos && raw.find("responder_usb_rx")!=std::string::npos && raw.find("dlc_rx")!=std::string::npos,"separate peer identity and trace missing");
    metadata.vehicleConnectionAllowed=true;
    require(!recorder.start(temp.path,metadata),"vehicle scope accepted for bench");
    metadata.vehicleConnectionAllowed=false; metadata.benchSchemaVersion=0;
    require(!recorder.start(temp.path,metadata),"bench flags silently accepted without schema");
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
    require(!recorder.waitUntilIdle(std::chrono::seconds(1)), "writer error reported as idle success");
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
    const auto idleWhileBlocked = recorder.waitUntilIdle(std::chrono::milliseconds(0));
    const auto overflow = recorder.enqueueRaw({3, 1, 3, "RX", {}, {3}});
    {
        std::lock_guard lock(mutex);
        release = true;
        wake.notify_all();
    }
    recorder.stop();
    require(!idleWhileBlocked, "in-flight writer reported idle while blocked");
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
        bridgeTraceMetadata();
        channelPolicyAndIdle();
        benchMetadata();
        visibleFailures();
        boundedQueue();
        std::cout << "recording: v2/v3 CSV/JSONL, partial provenance/age, Unicode paths, lifecycle, failures and bounds passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "recording test failed: " << exception.what() << '\n';
        return 1;
    }
}
