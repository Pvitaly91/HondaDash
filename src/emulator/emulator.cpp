#include "emulator/emulator.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace hd {
namespace {
double noise(std::uint32_t seed, Time tick) {
    auto value = seed ^ static_cast<std::uint32_t>(tick) ^ static_cast<std::uint32_t>(tick >> 32);
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return static_cast<double>(value % 1001) / 500.0 - 1.0;
}
}

const char* scenarioName(Scenario scenario) {
    switch (scenario) {
    case Scenario::Ignition: return "ignition";
    case Scenario::Idle: return "idle";
    case Scenario::Demo: return "demo";
    case Scenario::Manual: return "manual";
    }
    return "demo";
}

Emulator::Emulator() : manual_{850, 0, 85, 30, 2, 32, 14.2} {
    qualities_.fill(Quality::Valid);
}

void Emulator::setScenario(Scenario scenario, Time now) { scenario_ = scenario; epoch_ = now; }

void Emulator::setManual(Channel channel, double value) {
    const auto& info = channelInfo(channel);
    manual_.at(channelIndex(channel)) = std::isfinite(value) ? std::clamp(value, info.min, info.max) : info.min;
}

void Emulator::setChannelQuality(Channel channel, Quality quality) {
    qualities_.at(channelIndex(channel)) = quality == Quality::Unsupported ? Quality::Unsupported :
        quality == Quality::Valid ? Quality::Valid : Quality::Invalid;
}

void Emulator::start(Time now) { epoch_ = now; resetTransport(); }
void Emulator::resetTransport() { parser_.reset(); }

Sample Emulator::snapshot(const Frame& request, Time now) const {
    Sample sample;
    sample.time = now;
    sample.session = request.session;
    sample.request = request.request;
    sample.qualities = qualities_;
    const auto elapsed = now >= epoch_ ? now - epoch_ : 0;
    const auto jitter = noise(seed_, elapsed / 100);
    std::array<double, ChannelCount> values{};
    switch (scenario_) {
    case Scenario::Ignition:
        values = {0, 0, 22, 22, 0, 100, 12.5};
        break;
    case Scenario::Idle:
        values = {850 + 14 * jitter, 0, 85 + 0.2 * jitter, 30 + 0.1 * jitter,
            2, 32 + 0.7 * jitter, 14.2 + 0.02 * jitter};
        break;
    case Scenario::Manual:
        values = manual_;
        break;
    case Scenario::Demo: {
        // An 80-second synthetic cycle. Warm-up is intentionally accelerated.
        const auto phase = static_cast<double>(elapsed % 80000) / 1000.0;
        double rpm = 0, speed = 0, throttle = 0;
        double coolant = 22, intake = 22, map = 100, voltage = 12.5;
        if (phase >= 3 && phase < 5) {
            rpm = 250 + (phase - 3) * 525;
            throttle = 4;
            map = 55;
            voltage = 11.8 + (phase - 3) * 1.2;
        } else if (phase >= 5 && phase < 25) {
            const auto warm = (phase - 5) / 20;
            rpm = 1300 - warm * 450 + 14 * jitter;
            coolant = 22 + 63 * warm;
            intake = 22 + 8 * warm;
            throttle = 3 - warm;
            map = 34 - 2 * warm;
            voltage = 14.2;
        } else if (phase >= 25 && phase < 60) {
            const auto wave = (1 - std::cos((phase - 25) * 0.6)) / 2;
            throttle = 5 + 65 * wave;
            rpm = 850 + 4600 * wave + 14 * jitter;
            speed = std::max(0.0, (rpm - 850) / 45);
            coolant = 85 + 4 * wave;
            intake = 30 + 6 * wave;
            map = 32 + 63 * wave;
            voltage = 14.2 - 0.15 * wave;
        } else if (phase >= 60 && phase < 70) {
            rpm = 850 + 14 * jitter;
            coolant = 86;
            intake = 32;
            throttle = 2;
            map = 32;
            voltage = 14.2;
        } else if (phase >= 70) {
            coolant = 86 - (phase - 70) * 0.3;
            intake = 32 - (phase - 70) * 0.1;
        }
        values = {rpm, speed, coolant, intake, throttle, map, voltage};
        break;
    }
    }
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        if (sample.qualities[i] == Quality::Valid) sample.values[i] = values[i];
    }
    return sample;
}

std::vector<EmulatedReply> Emulator::consume(std::span<const std::uint8_t> requestBytes, Time now) {
    std::vector<EmulatedReply> replies;
    for (const auto& request : parser_.feed(requestBytes)) {
        if (faults_.silent) continue;
        Frame response{ErrorResponse, request.session, request.request, {1}};
        if (!request.payload.empty()) {
            response.payload = {2};
        } else if (request.type == Hello) {
            constexpr std::string_view identity = "synthetic-demo-v1";
            response.type = HelloResponse;
            response.payload = {ProtocolVersion};
            response.payload.insert(response.payload.end(), identity.begin(), identity.end());
        } else if (request.type == ReadSnapshot) {
            response.type = SnapshotResponse;
            response.payload = encodeSnapshot(snapshot(request, now));
        }
        auto bytes = encode(response);
        if (faults_.corruptNext) {
            bytes.back() ^= 0x80;
            faults_.corruptNext = false;
        }
        if (faults_.truncateNext) {
            bytes.resize(bytes.size() / 2);
            faults_.truncateNext = false;
        }
        replies.push_back({faults_.delayMs, std::move(bytes)});
    }
    return replies;
}

} // namespace hd
