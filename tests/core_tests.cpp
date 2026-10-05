#include "core/model.hpp"
#include "emulator/emulator.hpp"
#include "protocol/protocol.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* expression, int line) {
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) require(static_cast<bool>(expression), #expression, __LINE__)
void near(double value, double expected) { CHECK(std::abs(value - expected) < 0.00001); }

// These bytes are fixed independently of the production encoder. Header offsets and
// signed/scaled words were checked by hand; CRCs were cross-checked using polynomial
// long division and Python's independently implemented binascii.crc_hqx.
const std::vector<std::uint8_t> HelloFixture{
    0xa5,0x5a,0x01,0x01,0x04,0x03,0x02,0x01,0x44,0x33,0x22,0x11,0x00,0x65,0xe5};
const std::vector<std::uint8_t> HelloResponseFixture{
    0xa5,0x5a,0x01,0x81,0x04,0x03,0x02,0x01,0x44,0x33,0x22,0x11,0x12,
    0x01,0x73,0x79,0x6e,0x74,0x68,0x65,0x74,0x69,0x63,0x2d,0x64,0x65,0x6d,0x6f,0x2d,0x76,0x31,0x45,0x3c};
const std::vector<std::uint8_t> SnapshotFixture{
    0xa5,0x5a,0x01,0x82,0x04,0x03,0x02,0x01,0x44,0x33,0x22,0x11,0x15,
    0x01,0x00,0x00,0x01,0xd2,0x04,0x01,0x9c,0xff,0x01,0x4b,0x01,
    0x03,0x00,0x80,0x04,0x00,0x80,0x01,0x8c,0x05,0xb2,0x7b};

hd::Sample allValid(const std::array<double, hd::ChannelCount>& values) {
    hd::Sample sample;
    sample.time = 123;
    sample.session = 0x01020304;
    sample.request = 0x11223344;
    sample.qualities.fill(hd::Quality::Valid);
    for (std::size_t i = 0; i < hd::ChannelCount; ++i) sample.values[i] = values[i];
    return sample;
}

void goldenAndScaling() {
    const std::array<std::uint8_t, 9> check{'1','2','3','4','5','6','7','8','9'};
    CHECK(hd::crc16(check) == 0x29b1);
    CHECK(hd::crc16({}) == 0xffff);
    CHECK(hd::encode({hd::Hello, 0x01020304, 0x11223344, {}}) == HelloFixture);
    hd::Parser parser;
    const auto hello = parser.feed(HelloResponseFixture);
    CHECK(hello.size() == 1);
    CHECK(hello[0].type == hd::HelloResponse);
    CHECK(hello[0].session == 0x01020304);
    CHECK(hello[0].request == 0x11223344);
    CHECK(hello[0].payload == (std::vector<std::uint8_t>{1,'s','y','n','t','h','e','t','i','c','-','d','e','m','o','-','v','1'}));
    CHECK(hd::encode(hello[0]) == HelloResponseFixture);
    const auto frames = parser.feed(SnapshotFixture);
    CHECK(frames.size() == 1);
    const auto sample = hd::decodeSnapshot(frames[0], 700);
    CHECK(sample);
    CHECK(sample->time == 700 && sample->session == 0x01020304 && sample->request == 0x11223344);
    near(*sample->values[0], 0);
    near(*sample->values[1], 123.4);
    near(*sample->values[2], -10);
    near(*sample->values[3], 33.1);
    CHECK(sample->qualities[4] == hd::Quality::Unsupported && !sample->values[4]);
    CHECK(sample->qualities[5] == hd::Quality::Invalid && !sample->values[5]);
    near(*sample->values[6], 14.20);
    CHECK(hd::encode({hd::SnapshotResponse, sample->session, sample->request, hd::encodeSnapshot(*sample)}) == SnapshotFixture);

    // Independent raw words cover every scale, both signed temperatures and all bounds.
    const auto lower = allValid({0,0,-40,-40,0,0,0});
    const auto upper = allValid({12000,250,150,120,100,250,20});
    const auto typical = allValid({2345,67.8,-12.3,28.4,56.7,98.7,13.45});
    const std::vector<std::uint8_t> lowerBytes{
        1,0,0, 1,0,0, 1,0x70,0xfe, 1,0x70,0xfe, 1,0,0, 1,0,0, 1,0,0};
    const std::vector<std::uint8_t> upperBytes{
        1,0xe0,0x2e, 1,0xc4,0x09, 1,0xdc,0x05, 1,0xb0,0x04,
        1,0xe8,0x03, 1,0xc4,0x09, 1,0xd0,0x07};
    const std::vector<std::uint8_t> typicalBytes{
        1,0x29,0x09, 1,0xa6,0x02, 1,0x85,0xff, 1,0x1c,0x01,
        1,0x37,0x02, 1,0xdb,0x03, 1,0x41,0x05};
    CHECK(hd::encodeSnapshot(lower) == lowerBytes);
    CHECK(hd::encodeSnapshot(upper) == upperBytes);
    CHECK(hd::encodeSnapshot(typical) == typicalBytes);
    const std::array<const hd::Sample*, 3> inputs{&lower, &upper, &typical};
    const std::array<const std::vector<std::uint8_t>*, 3> payloads{&lowerBytes, &upperBytes, &typicalBytes};
    for (std::size_t n = 0; n < inputs.size(); ++n) {
        const auto decoded = hd::decodeSnapshot({hd::SnapshotResponse, 7, 8, *payloads[n]}, 9);
        CHECK(decoded);
        for (std::size_t i = 0; i < hd::ChannelCount; ++i) {
            CHECK(decoded->qualities[i] == hd::Quality::Valid);
            near(*decoded->values[i], *inputs[n]->values[i]);
        }
    }
    auto malformed = typicalBytes;
    malformed[0] = 2;
    CHECK(!hd::decodeSnapshot({hd::SnapshotResponse, 1, 1, malformed}, 0));
    malformed = typicalBytes;
    malformed[1] = 0;
    malformed[2] = 0x80;
    CHECK(!hd::decodeSnapshot({hd::SnapshotResponse, 1, 1, malformed}, 0));
    malformed = typicalBytes;
    malformed[0] = 3; // Non-valid entries must contain the absent sentinel.
    CHECK(!hd::decodeSnapshot({hd::SnapshotResponse, 1, 1, malformed}, 0));
    CHECK(!hd::decodeSnapshot({hd::HelloResponse, 1, 1, typicalBytes}, 0));
    malformed.pop_back();
    CHECK(!hd::decodeSnapshot({hd::SnapshotResponse, 1, 1, malformed}, 0));
    malformed = typicalBytes;
    malformed[1] = 0xff;
    malformed[2] = 0x7f;
    const auto outside = hd::decodeSnapshot({hd::SnapshotResponse, 1, 1, malformed}, 0);
    CHECK(outside && outside->qualities[0] == hd::Quality::Invalid && !outside->values[0]);
    auto invalid = typical;
    invalid.values[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK(hd::encodeSnapshot(invalid)[0] == 4);
    invalid.values[0] = -1;
    CHECK(hd::encodeSnapshot(invalid)[0] == 4);
    bool threw = false;
    try { (void)hd::encode({hd::Hello, 1, 1, std::vector<std::uint8_t>(65)}); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

void streamingAndRecovery() {
    hd::Parser byteParser;
    std::vector<hd::Frame> byteFrames;
    for (const auto byte : SnapshotFixture) {
        const std::array<std::uint8_t, 1> one{byte};
        auto arrived = byteParser.feed(one);
        byteFrames.insert(byteFrames.end(), arrived.begin(), arrived.end());
        CHECK(byteParser.buffered() <= hd::MaxFrame);
    }
    CHECK(byteFrames.size() == 1 && byteParser.buffered() == 0 && byteParser.errors() == 0);
    std::vector<std::uint8_t> combined{0,0xa5,0,0x5a,0xff};
    combined.insert(combined.end(), HelloFixture.begin(), HelloFixture.end());
    combined.insert(combined.end(), SnapshotFixture.begin(), SnapshotFixture.end());
    hd::Parser combinedParser;
    CHECK(combinedParser.feed(combined).size() == 2);

    for (unsigned fault = 0; fault < 3; ++fault) {
        auto broken = SnapshotFixture;
        if (fault == 0) broken.back() ^= 1;
        if (fault == 1) broken[2] = 2;
        if (fault == 2) broken[12] = 65;
        hd::Parser parser;
        CHECK(parser.feed(broken).empty());
        CHECK(parser.errors() >= 1);
        CHECK(parser.feed(HelloFixture).size() == 1);
        CHECK(parser.buffered() == 0);
        const auto errors = parser.errors();
        parser.reset();
        CHECK(parser.errors() == errors);
    }

    const auto maximum = hd::encode({0x33, 10, 20, std::vector<std::uint8_t>(64, 0)});
    CHECK(maximum.size() == hd::MaxFrame);
    // Check every truncation boundary, including a declared payload longer than
    // the truncated prefix plus the following valid frame.
    for (std::size_t prefix = 1; prefix < maximum.size(); ++prefix) {
        hd::Parser parser;
        CHECK(parser.feed(std::span(maximum).first(prefix)).empty());
        const auto recovered = parser.feed(HelloFixture);
        CHECK(recovered.size() == 1 && recovered[0].type == hd::Hello);
        CHECK(parser.buffered() == 0);
    }
    auto embeddedSof = maximum;
    embeddedSof[20] = 0xa5;
    embeddedSof[21] = 0x5a;
    embeddedSof[22] = 1;
    const auto embeddedCrc = hd::crc16(std::span(embeddedSof).subspan(2, embeddedSof.size() - 4));
    embeddedSof[embeddedSof.size() - 2] = static_cast<std::uint8_t>(embeddedCrc);
    embeddedSof.back() = static_cast<std::uint8_t>(embeddedCrc >> 8);
    hd::Parser embedded;
    CHECK(embedded.feed(embeddedSof).size() == 1);

    hd::Parser sustained;
    const std::array<std::uint8_t, 8> noise{0,0xff,0xa5,0xa5,0x5a,2,0,0};
    for (std::size_t i = 0; i < 50000; ++i) {
        CHECK(sustained.feed(noise).empty());
        CHECK(sustained.buffered() <= hd::MaxFrame);
        if (i % 100 == 0) CHECK(sustained.feed(HelloFixture).size() == 1);
    }
    sustained.reset();
    CHECK(sustained.buffered() == 0);
}

void freshness() {
    hd::Model model;
    for (std::size_t i = 0; i < hd::ChannelCount; ++i) {
        CHECK(model.channels()[i].quality == hd::Quality::NoData);
        CHECK(!model.current(static_cast<hd::Channel>(i), 0));
    }
    auto sample = allValid({0,0,-40,-40,0,0,0});
    sample.time = 100;
    model.apply(sample);
    CHECK(model.current(hd::Channel::Rpm, 100) == 0);
    CHECK(model.channels()[0].lastValid == 100);
    CHECK(model.channels()[0].session == sample.session && model.channels()[0].request == sample.request);
    model.refresh(1100);
    CHECK(model.channels()[0].quality == hd::Quality::Valid);
    model.refresh(1101);
    CHECK(model.channels()[0].quality == hd::Quality::Stale);
    CHECK(model.current(hd::Channel::Rpm, 3100) == 0);
    CHECK(!model.current(hd::Channel::Rpm, 3101));
    CHECK(model.channels()[0].value == 0); // Hidden current value does not invent a sample.
    sample.time = 4000;
    sample.qualities[1] = hd::Quality::Unsupported;
    sample.values[1].reset();
    sample.qualities[2] = hd::Quality::Invalid;
    sample.values[2].reset();
    model.apply(sample);
    CHECK(model.channels()[0].quality == hd::Quality::Valid);
    CHECK(model.channels()[0].lastValid == 4000);
    CHECK(model.channels()[1].quality == hd::Quality::Unsupported && !model.current(hd::Channel::Speed, 4000));
    CHECK(model.channels()[2].quality == hd::Quality::Invalid && !model.current(hd::Channel::Coolant, 4000));
    CHECK(model.channels()[1].lastValid == 100 && model.channels()[2].lastValid == 100);
    model.refresh(6000);
    CHECK(model.channels()[0].quality == hd::Quality::Stale);
    CHECK(model.channels()[1].quality == hd::Quality::Unsupported);
    CHECK(model.channels()[2].quality == hd::Quality::Invalid);
    model.reset();
    CHECK(model.channels()[0].quality == hd::Quality::NoData && !model.channels()[0].lastValid);
    hd::Model fast({10,20});
    fast.apply(sample);
    fast.refresh(4011);
    CHECK(fast.channels()[0].quality == hd::Quality::Stale);
    CHECK(!fast.current(hd::Channel::Rpm, 4021));
    bool threw = false;
    try { hd::Model bad({20,10}); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

hd::Sample read(hd::Emulator& emulator, hd::Time time) {
    const auto request = hd::encode({hd::ReadSnapshot, 55, 66, {}});
    const auto replies = emulator.consume(request, time);
    CHECK(replies.size() == 1);
    hd::Parser parser;
    const auto frames = parser.feed(replies[0].bytes);
    CHECK(frames.size() == 1);
    const auto decoded = hd::decodeSnapshot(frames[0], time);
    CHECK(decoded);
    return *decoded;
}

void emulatorPath() {
    hd::Emulator emulator;
    const auto helloReply = emulator.consume(HelloFixture, 0);
    CHECK(helloReply.size() == 1 && helloReply[0].bytes == HelloResponseFixture);
    emulator.setScenario(hd::Scenario::Ignition);
    const auto ignition = read(emulator, 0);
    near(*ignition.values[0], 0);
    near(*ignition.values[5], 100);
    emulator.setScenario(hd::Scenario::Idle);
    const auto idle = read(emulator, 100);
    CHECK(*idle.values[0] >= 836 && *idle.values[0] <= 864 && *idle.values[1] == 0);

    emulator.setScenario(hd::Scenario::Manual);
    hd::Model model;
    auto before = read(emulator, 200);
    model.apply(before);
    emulator.setManual(hd::Channel::Rpm, 4567);
    CHECK(model.current(hd::Channel::Rpm, 200) == before.values[0]);
    const auto request = hd::encode({hd::ReadSnapshot, 70, 80, {}});
    CHECK(emulator.consume(std::span(request).first(5), 300).empty());
    const auto reply = emulator.consume(std::span(request).subspan(5), 300);
    CHECK(reply.size() == 1);
    hd::Parser parser;
    std::vector<hd::Frame> received;
    for (const auto byte : reply[0].bytes) {
        const std::array<std::uint8_t, 1> fragment{byte};
        const auto frames = parser.feed(fragment);
        received.insert(received.end(), frames.begin(), frames.end());
    }
    CHECK(received.size() == 1);
    const auto changed = hd::decodeSnapshot(received[0], 300);
    CHECK(changed && changed->session == 70 && changed->request == 80);
    model.apply(*changed);
    CHECK(model.current(hd::Channel::Rpm, 300) == 4567);
    emulator.setManual(hd::Channel::Rpm, 99999);
    near(*read(emulator, 400).values[0], 12000);
    emulator.setChannelQuality(hd::Channel::Speed, hd::Quality::Unsupported);
    emulator.setChannelQuality(hd::Channel::Coolant, hd::Quality::Invalid);
    const auto unavailable = read(emulator, 500);
    CHECK(!unavailable.values[1] && unavailable.qualities[1] == hd::Quality::Unsupported);
    CHECK(!unavailable.values[2] && unavailable.qualities[2] == hd::Quality::Invalid);
    emulator.setChannelQuality(hd::Channel::Speed, hd::Quality::Valid);
    CHECK(read(emulator, 600).values[1]);

    emulator.faults().silent = true;
    CHECK(emulator.consume(request, 700).empty());
    emulator.faults().silent = false;
    emulator.faults().delayMs = 345;
    CHECK(emulator.consume(request, 700)[0].delayMs == 345);
    emulator.faults().corruptNext = true;
    const auto corrupted = emulator.consume(request, 800);
    CHECK(corrupted.size() == 1 && !emulator.faults().corruptNext);
    CHECK(parser.feed(corrupted[0].bytes).empty());
    CHECK(parser.errors() == 1);
    CHECK(emulator.consume(request, 800)[0].bytes != corrupted[0].bytes);
    emulator.faults().truncateNext = true;
    const auto truncated = emulator.consume(request, 900);
    CHECK(truncated[0].bytes.size() == 18 && !emulator.faults().truncateNext);
    CHECK(parser.feed(truncated[0].bytes).empty());
    const auto restored = emulator.consume(request, 1000);
    CHECK(parser.feed(restored[0].bytes).size() == 1);
    const auto unknown = emulator.consume(hd::encode({0x20, 70, 80, {}}), 1100);
    const auto errors = parser.feed(unknown[0].bytes);
    CHECK(errors.size() == 1 && errors[0].type == hd::ErrorResponse && errors[0].payload == std::vector<std::uint8_t>{1});
    const auto badPayload = emulator.consume(hd::encode({hd::Hello, 70, 80, {1}}), 1100);
    CHECK(parser.feed(badPayload[0].bytes)[0].payload == std::vector<std::uint8_t>{2});

    hd::Emulator a, b;
    a.setSeed(123);
    b.setSeed(123);
    a.setScenario(hd::Scenario::Demo, 1000);
    b.setScenario(hd::Scenario::Demo, 1000);
    for (hd::Time time = 1000; time < 161000; time += 137) {
        const auto first = a.consume(request, time);
        const auto second = b.consume(request, time);
        CHECK(first[0].bytes == second[0].bytes);
        // The function is a pure mapping of seed, scenario and elapsed model time;
        // repeated requests or skipped time points cannot alter later values.
        CHECK(a.consume(request, time)[0].bytes == first[0].bytes);
    }
    CHECK(*read(a, 1000).values[0] == 0);
    CHECK(*read(a, 31000).values[0] > 850);
    CHECK(*read(a, 76000).values[0] == 0);
    a.start(200000);
    CHECK(*read(a, 200000).values[0] == 0);
    CHECK(a.consume(std::span(request).first(5), 200001).empty());
    a.resetTransport();
    CHECK(a.consume(std::span(request).subspan(5), 200001).empty());
    CHECK(a.consume(request, 200002).size() == 1);
}
}

int main() {
    struct Test { const char* name; void (*run)(); };
    const std::array tests{Test{"golden/scales", goldenAndScaling}, Test{"stream/recovery/bounds", streamingAndRecovery},
        Test{"freshness", freshness}, Test{"emulator/byte-path/determinism", emulatorPath}};
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "FAIL " << test.name << ": " << exception.what() << '\n';
            return 1;
        }
    }
    return 0;
}
