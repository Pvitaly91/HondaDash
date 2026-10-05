#include "honda_dlc/session.hpp"
#include "recording/recorder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace hd;
namespace d = hd::dlc;
void require(bool result, const char *expression, int line) {
    if (!result)
        throw std::runtime_error(std::to_string(line) + ": " + expression);
}
#define CHECK(...) require(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
// Fixed, independently calculated fixtures: docs/HONDA_DLC_OFFLINE_TESTING.md.
// Byte sums modulo 256 are zero. None of these fixtures was captured from an ECU.
constexpr std::array<std::uint8_t, 5> RpmRequest{0x20, 0x05, 0x00, 0x02, 0xd9};
constexpr std::array<std::uint8_t, 5> TpsRequest{0x20, 0x05, 0x14, 0x01, 0xc6};
constexpr std::array<std::uint8_t, 5> EctRequest{0x20, 0x05, 0x10, 0x01, 0xca};
constexpr std::array<std::uint8_t, 5> RpmResponse{0x00, 0x05, 0x09, 0xc3, 0x2f};
constexpr std::array<std::uint8_t, 4> TpsResponse{0x00, 0x04, 0x58, 0xa4};
constexpr std::array<std::uint8_t, 4> EctResponse{0x00, 0x04, 0x40, 0xbc};
constexpr std::array<std::uint8_t, 11> Wake{0x68, 0x6a, 0xf5, 0xaf, 0xbf, 0xb3, 0xb2, 0xc1, 0xdb, 0xb3, 0xe9};
void advance(d::Session &session, Time from, Time to) {
    for (Time now = from; now <= to; ++now)
        session.tick(now);
}
std::string readFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
void goldenCodec() {
    CHECK(d::Initialization == Wake);
    CHECK(d::encode(d::Operation::Read, {0, 2}) == RpmRequest);
    CHECK(d::encode(d::Operation::Read, {0x14, 1}) == TpsRequest);
    CHECK(d::encode(d::Operation::Read, {0x10, 1}) == EctRequest);
    CHECK(d::additiveChecksum(std::span(RpmRequest).first(4)) == 0xd9);
    CHECK(d::additiveChecksum(RpmResponse) == 0);
    for (const auto operation : {d::Operation::Write, d::Operation::Reset})
        CHECK(!d::encode(operation, {0, 2}));
    for (const auto read : {d::Read{0, 0}, {0, 1}, {1, 2}, {0x10, 16}, {255, 2}, {65535, 65535}})
        CHECK(!d::encode(d::Operation::Read, read));
    for (unsigned address = 0; address < 256; ++address)
        for (unsigned count = 0; count < 4; ++count)
            CHECK(d::encode(d::Operation::Read,
                            {static_cast<std::uint16_t>(address), static_cast<std::uint16_t>(count)})
                      .has_value() ==
                  ((address == 0 && count == 2) || ((address == 0x10 || address == 0x14) && count == 1)));
    d::Parser parser;
    parser.expect(2);
    for (std::size_t i = 0; i < RpmResponse.size(); ++i) {
        CHECK(parser.feed(RpmResponse[i]) ==
              (i + 1 == RpmResponse.size() ? d::Parser::Result::Complete : d::Parser::Result::Incomplete));
        CHECK(parser.buffered() <= d::Parser::Capacity);
    }
    CHECK(parser.payload().size() == 2 && parser.payload()[0] == 9 && parser.payload()[1] == 0xc3);
    for (int repeat = 0; repeat < 2; ++repeat)
        for (auto byte : RpmResponse)
            parser.feed(byte);
    CHECK(parser.payload().size() == 2);
    parser.reset();
    CHECK(parser.feed(0x7e) == d::Parser::Result::HeaderError);
    CHECK(parser.feed(0) == d::Parser::Result::Incomplete);
    CHECK(parser.feed(255) == d::Parser::Result::LengthError);
    auto bad = RpmResponse;
    bad.back() ^= 1;
    d::Parser::Result status{};
    for (auto byte : bad)
        status = parser.feed(byte);
    CHECK(status == d::Parser::Result::ChecksumError);
    CHECK(parser.payload().empty());
    parser.feed(0);
    parser.feed(5);
    parser.feed(9);
    CHECK(parser.buffered() == 3 && parser.payload().empty());
    parser.reset();
    for (unsigned i = 0; i < 100000; ++i) {
        parser.feed(static_cast<std::uint8_t>(i));
        CHECK(parser.buffered() <= 5);
    }
    parser.expect(1);
    for (const auto bytes :
         {std::span<const std::uint8_t>(TpsResponse), std::span<const std::uint8_t>(EctResponse)}) {
        for (auto byte : bytes)
            status = parser.feed(byte);
        CHECK(status == d::Parser::Result::Complete);
    }
}
void goldenProfile() {
    const auto decode = [](d::Read read, std::initializer_list<std::uint8_t> bytes, Channel channel) {
        auto sample = d::decode(read, std::span(bytes.begin(), bytes.size()), 100, 7, 9);
        CHECK(sample.has_value());
        CHECK(sample->updatedMask == (1u << channelIndex(channel)));
        CHECK(sample->source == d::ProfileId && sample->request == 9 && sample->session == 7);
        return *sample;
    };
    CHECK(decode({0, 2}, {9, 0xc3}, Channel::Rpm).values[0] == 750);
    CHECK(decode({0, 2}, {4, 0xe1}, Channel::Rpm).values[0] == 1500);
    CHECK(decode({0, 2}, {0, 0}, Channel::Rpm).values[0] == 1875000);
    CHECK(decode({0, 2}, {0, 100}, Channel::Rpm).values[0] == 18564); // above the demo gauge range
    CHECK(decode({0, 2}, {0x80, 0}, Channel::Rpm).values[0] == 57);   // unsigned wide denominator
    CHECK(decode({0, 2}, {0xff, 0xfe}, Channel::Rpm).values[0] == 28);
    auto unknown = decode({0, 2}, {0xff, 0xff}, Channel::Rpm);
    CHECK(!unknown.values[0] && unknown.qualities[0] == Quality::Invalid);
    CHECK(decode({0x10, 1}, {64}, Channel::Coolant).values[2] == 61); // not older rounded 62
    CHECK(decode({0x10, 1}, {32}, Channel::Coolant).values[2] == 89);
    // Independent decimal evaluations of the published coefficients; no production
    // encoder or polynomial computes these expected literals during the test.
    CHECK(decode({0x10, 1}, {0}, Channel::Coolant).values[2] == 155);
    CHECK(decode({0x10, 1}, {16}, Channel::Coolant).values[2] == 115);
    CHECK(decode({0x10, 1}, {128}, Channel::Coolant).values[2] == 33);
    CHECK(decode({0x10, 1}, {192}, Channel::Coolant).values[2] == 7);
    CHECK(decode({0x10, 1}, {224}, Channel::Coolant).values[2] == -9); // truncation, not floor(-9.416...)
    CHECK(decode({0x10, 1}, {255}, Channel::Coolant).values[2] == -43);
    CHECK(decode({0x14, 1}, {88}, Channel::Throttle).values[4] == 32);
    CHECK(decode({0x14, 1}, {174}, Channel::Throttle).values[4] == 75);
    CHECK(decode({0x14, 1}, {24}, Channel::Throttle).values[4] == 0);
    CHECK(decode({0x14, 1}, {25}, Channel::Throttle).values[4] == 0);
    CHECK(decode({0x14, 1}, {224}, Channel::Throttle).values[4] == 100);
    CHECK(decode({0x14, 1}, {23}, Channel::Throttle).qualities[4] == Quality::Invalid);
    CHECK(decode({0x14, 1}, {225}, Channel::Throttle).qualities[4] == Quality::Invalid);
    CHECK(!d::decode({0, 2}, std::span(TpsResponse).subspan(2, 1), 0, 0, 0));
    Model model;
    model.apply(d::unavailable(0, 1));
    CHECK(model.channels()[0].quality == Quality::NoData);
    CHECK(model.channels()[1].quality == Quality::Unsupported);
    CHECK(model.channels()[1].reason == "Не визначено для цього профілю");
    model.apply(decode({0, 2}, {0, 0}, Channel::Rpm));
    model.apply(decode({0x10, 1}, {255}, Channel::Coolant));
    CHECK(model.current(Channel::Rpm, 100) == 1875000);
    CHECK(model.current(Channel::Coolant, 100) == -43); // no demo widget domain clamp
}
void independentResponderAndLink() {
    d::ScriptedHondaEcu ecu;
    CHECK(ecu.receive(RpmRequest, 0).empty());
    CHECK(ecu.rejected() == 1);
    CHECK(ecu.receive(Wake, 0).empty());
    CHECK(ecu.receive(RpmRequest, 299).empty());
    CHECK(ecu.receive(RpmRequest, 300) == std::vector<std::uint8_t>(RpmResponse.begin(), RpmResponse.end()));
    CHECK(ecu.receive(EctRequest, 310) == std::vector<std::uint8_t>(EctResponse.begin(), EctResponse.end()));
    CHECK(ecu.receive(TpsRequest, 320) == std::vector<std::uint8_t>(TpsResponse.begin(), TpsResponse.end()));
    auto bad = RpmRequest;
    bad.back() ^= 1;
    CHECK(ecu.receive(bad, 330).empty());
    d::OfflineLink link;
    link.send(Wake, 0);
    bool overflow = false;
    for (int i = 0; i < 100; ++i)
        if (!link.send(RpmRequest, 300))
            overflow = true;
    CHECK(overflow && link.pendingBytes() <= d::OfflineLink::Capacity);
    std::size_t delivered = 0;
    link.tick(1000, [&](auto bytes, Time) { delivered += bytes.size(); });
    CHECK(delivered == 60 && link.pendingBytes() == 0);
    link.newExperiment();
    link.send(Wake, 0);
    link.setFaults({false, 300});
    link.send(EctRequest, 300);
    d::Parser parser;
    parser.expect(1);
    parser.reset();
    std::vector<std::uint8_t> late;
    link.tick(610, [&](auto bytes, Time) { late.insert(late.end(), bytes.begin(), bytes.end()); });
    CHECK(late == std::vector<std::uint8_t>(EctResponse.begin(), EctResponse.end()));
}
void productionSession() {
    static_assert(!std::is_constructible_v<d::Session, Transport &>);
    CHECK(!d::LiveEnabled && !d::HardwareVerified);
    d::Session session({}, {200, 500});
    std::vector<RawEvent> raw;
    std::vector<Sample> samples;
    session.onRaw = [&](const RawEvent &event) { raw.push_back(event); };
    session.onSample = [&](const Sample &sample) { samples.push_back(sample); };
    session.start(0);
    CHECK(session.state() == d::State::Initializing && session.stats().accepted == 0);
    CHECK(raw.size() == 3 && raw.back().kind == "tx" &&
          raw.back().bytes == std::vector<std::uint8_t>(Wake.begin(), Wake.end()));
    session.tick(299);
    CHECK(session.stats().accepted == 0);
    advance(session, 300, 350);
    CHECK(session.state() == d::State::Polling && session.stats().accepted == 3);
    CHECK(session.model().current(Channel::Rpm, 350) == 750);
    CHECK(session.model().current(Channel::Coolant, 350) == 61);
    CHECK(session.model().current(Channel::Throttle, 350) == 32);
    const auto coolantStamp = session.model().channels()[2].lastValid;
    const auto coolantRequest = session.model().channels()[2].request;
    advance(session, 351, 900);
    CHECK(session.model().channels()[2].lastValid == coolantStamp);
    CHECK(session.model().channels()[2].request == coolantRequest);
    CHECK(session.model().channels()[2].quality == Quality::Stale);
    CHECK(!session.model().current(Channel::Coolant, 900));
    CHECK(session.model().channels()[0].quality == Quality::Valid);
    CHECK(session.stats().channelHz[0] > session.stats().channelHz[2]);
    session.setScenario(d::Scenario::Higher);
    advance(session, 901, 1400);
    CHECK(session.model().current(Channel::Rpm, 1400) == 1500);
    CHECK(session.model().current(Channel::Coolant, 1400) == 89);
    CHECK(session.model().current(Channel::Throttle, 1400) == 75);
    CHECK(samples[1].updatedMask == 1 && samples[2].updatedMask == 16 && samples[3].updatedMask == 4);
    const auto txCount = std::count_if(raw.begin(), raw.end(), [](const auto &e) { return e.kind == "tx"; });
    CHECK(!session.request(d::Operation::Write, {0, 2}, 1400));
    CHECK(!session.request(d::Operation::Reset, {0, 2}, 1400));
    CHECK(!session.request(d::Operation::Read, {1, 2}, 1400));
    CHECK(std::count_if(raw.begin(), raw.end(), [](const auto &e) { return e.kind == "tx"; }) == txCount);
    CHECK(!session.setProfile("honda-p28-unverified", 1400));
    session.start(1401);
    CHECK(session.state() == d::State::Stopped);
    CHECK(session.model().channels()[0].quality == Quality::NoData);
    CHECK(session.setProfile(d::ProfileId, 1402));
    session.setScenario(d::Scenario::Boundary);
    session.start(1403);
    advance(session, 1404, 1750);
    CHECK(session.model().channels()[0].quality == Quality::Invalid);
    CHECK(session.model().current(Channel::Throttle, 1750) == 0);
    CHECK(session.model().current(Channel::Coolant, 1750) == -43);
    for (Time t = 2000; t < 2050; ++t) {
        session.stop(t);
        session.start(t);
    }
    CHECK(session.stats().accepted == 0 && session.pendingBytes() == 0);
}
void faultsAndAmbiguity() {
    std::array<d::Faults, 5> faults{};
    faults[0].corruptNext = true;
    faults[1].wrongLengthNext = true;
    faults[2].truncateNext = true;
    faults[3].noiseNext = true;
    faults[4].silent = true;
    for (const auto fault : faults) {
        d::Session session;
        session.setFaults(fault);
        session.start(0);
        advance(session, 1, 600);
        CHECK(session.state() == d::State::Faulted && session.stats().accepted == 0);
        CHECK(!session.model().current(Channel::Rpm, 600));
        CHECK(session.error().find("offline") != std::string::npos);
        session.setFaults({});
        session.start(601);
        advance(session, 602, 960);
        CHECK(session.stats().accepted == 3);
    }
    // A = ECT, B = TPS; both four-byte replies contain neither address nor ID.
    d::Session session({200, 50, 10000, 10000});
    std::vector<RawEvent> raw;
    session.onRaw = [&](const RawEvent &event) { raw.push_back(event); };
    session.start(0);
    advance(session, 1, 350);
    const auto accepted = session.stats().accepted;
    const auto ectStamp = session.model().channels()[2].lastValid;
    const auto tpsStamp = session.model().channels()[4].lastValid;
    session.setFaults({false, 300});
    CHECK(session.request(d::Operation::Read, {0x10, 1}, 400));
    session.tick(599);
    CHECK(session.state() == d::State::Polling);
    session.tick(600); // exact total deadline; fault resets only local parser
    CHECK(session.state() == d::State::Faulted && session.pendingBytes() == 4);
    CHECK(!session.request(d::Operation::Read, {0x14, 1}, 601)); // B MUST NOT start
    session.tick(710);
    CHECK(session.stats().accepted == accepted && session.stats().ignored == 4);
    CHECK(session.model().channels()[2].lastValid == ectStamp);
    CHECK(session.model().channels()[4].lastValid == tpsStamp);
    std::vector<std::uint8_t> drained;
    for (const auto &event : raw)
        if (event.kind == "rx" && event.time == 710) {
            CHECK(event.request == 0 && event.detail.find("unassociated") != std::string::npos);
            drained.insert(drained.end(), event.bytes.begin(), event.bytes.end());
        }
    CHECK(drained == std::vector<std::uint8_t>(EctResponse.begin(), EctResponse.end()));
    session.setFaults({});
    session.start(800);
    advance(session, 801, 1150);
    CHECK(session.stats().accepted == 3 && session.state() == d::State::Polling);
    // Profile change cancels an outstanding old response without allowing updates.
    session.setFaults({false, 100});
    CHECK(session.request(d::Operation::Read, {0x14, 1}, 1200));
    CHECK(session.setProfile(d::ProfileId, 1201));
    session.tick(1400);
    CHECK(session.model().channels()[4].quality == Quality::NoData);
    CHECK(session.pendingBytes() == 0);
    // Interbyte deadline has its own meaning, independently of the 200 ms total.
    d::Session truncated;
    d::Faults truncation;
    truncation.truncateNext = true;
    truncated.setFaults(truncation);
    truncated.start(0);
    advance(truncated, 1, 306);
    truncated.tick(355);
    CHECK(truncated.state() == d::State::Polling);
    truncated.tick(356);
    CHECK(truncated.state() == d::State::Faulted);
}
void callbackCancellation() {
    d::Session session;
    session.onRaw = [&](const RawEvent &event) {
        if (event.kind == "tx_queued" && event.request)
            session.stop(event.time);
    };
    session.start(0);
    session.tick(300);
    CHECK(session.state() == d::State::Stopped && session.pendingBytes() == 0);
    session.onRaw = {};
    session.onSample = [&](const Sample &sample) {
        if (sample.request)
            session.stop(sample.time);
    };
    session.start(400);
    advance(session, 401, 1000);
    CHECK(session.state() == d::State::Stopped && session.stats().accepted == 1);
    session.onSample = {};
    session.onRaw = [&](const RawEvent &event) {
        if (event.kind == "tx" && event.request)
            session.stop(event.time);
    };
    session.start(1100);
    session.tick(1400);
    CHECK(session.state() == d::State::Stopped && session.pendingBytes() == 5);
    session.tick(1420);
    CHECK(session.pendingBytes() == 0 && session.stats().ignored == 5);
    CHECK(session.model().channels()[0].quality == Quality::NoData);
}
void endToEndRecording() {
    const auto directory =
        std::filesystem::temp_directory_path() / std::filesystem::path(u8"HondaDash-M2a-тест");
    // Only delete these known test-owned files, never recursively remove a directory.
    std::filesystem::create_directories(directory);
    d::Session session;
    Recorder recorder;
    RecordingMetadata metadata;
    metadata.scenario = d::scenarioId(d::Scenario::Baseline);
    metadata.transport = "offline-in-memory";
    metadata.endpoint = "scripted-honda-ecu";
    metadata.formatVersion = 3;
    metadata.wireProtocol = "honda-dlc";
    metadata.profile = d::ProfileId;
    metadata.profileVersion = 1;
    metadata.evidenceStatus = "reference-derived; hardware-unverified";
    metadata.fixtureClass = "reference-derived";
    metadata.fixtureId = metadata.scenario;
    CHECK(recorder.start(directory, metadata));
    session.onRaw = [&](const RawEvent &event) { CHECK(recorder.enqueueRaw(event)); };
    session.onSample = [&](const Sample &sample) { CHECK(recorder.enqueueSample(sample)); };
    session.start(0);
    advance(session, 1, 350);
    d::Faults bad;
    bad.corruptNext = true;
    session.setFaults(bad);
    advance(session, 351, 500);
    CHECK(session.state() == d::State::Faulted);
    session.stop(501);
    recorder.stop();
    CHECK(recorder.error().empty());
    const auto output = recorder.directory();
    const auto raw = readFile(output / "raw.jsonl");
    const auto csv = readFile(output / "measurements.csv");
    CHECK(raw.find("\"format_version\":3") != std::string::npos);
    CHECK(raw.find("\"source\":\"simulation\"") != std::string::npos);
    CHECK(raw.find("honda-dlc-kerpz-obd1-reference-v1") != std::string::npos);
    CHECK(raw.find("reference-derived") != std::string::npos);
    CHECK(raw.find("transaction_fault") != std::string::npos);
    CHECK(raw.find("partial_sample") != std::string::npos);
    CHECK(raw.find("host_") != std::string::npos);
    CHECK(csv.find("updated") != std::string::npos && csv.find("age_ms") != std::string::npos);
    CHECK(raw.find("\"bytes_hex\":\"20050002d9\"") != std::string::npos);
    CHECK(raw.find("\"bytes_hex\":\"2e\"") != std::string::npos); // corrupt RPM checksum, golden 2F xor 01
    CHECK(raw.find("\"hardware_verified\":false") != std::string::npos);
    CHECK(raw.find("\"live_enabled\":false") != std::string::npos);
    std::filesystem::remove(output / "raw.jsonl");
    std::filesystem::remove(output / "measurements.csv");
    std::filesystem::remove(output);
}
} // namespace
int main() {
    try {
        goldenCodec();
        goldenProfile();
        independentResponderAndLink();
        productionSession();
        faultsAndAmbiguity();
        callbackCancellation();
        endToEndRecording();
        std::cout << "Honda DLC independent goldens, offline session, faults, ambiguity, recording PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
