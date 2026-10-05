#pragma once

#include "protocol/protocol.hpp"

namespace hd {

enum class Scenario : std::uint8_t { Ignition, Idle, Demo, Manual };
const char* scenarioName(Scenario scenario);

struct FaultSettings {
    bool silent{};
    Time delayMs{};
    bool corruptNext{};
    bool truncateNext{};
};

struct EmulatedReply {
    Time delayMs{};
    std::vector<std::uint8_t> bytes;
};

class Emulator {
public:
    Emulator();
    void setScenario(Scenario scenario, Time now = 0);
    Scenario scenario() const { return scenario_; }
    void setSeed(std::uint32_t seed) { seed_ = seed; }
    std::uint32_t seed() const { return seed_; }
    void setManual(Channel channel, double value);
    void setChannelQuality(Channel channel, Quality quality);
    FaultSettings& faults() { return faults_; }
    const FaultSettings& faults() const { return faults_; }
    void start(Time now);
    void resetTransport();
    std::vector<EmulatedReply> consume(std::span<const std::uint8_t> requestBytes, Time now);

private:
    Sample snapshot(const Frame& request, Time now) const;
    Scenario scenario_{Scenario::Demo};
    Time epoch_{};
    std::uint32_t seed_{1};
    std::array<double, ChannelCount> manual_{};
    std::array<Quality, ChannelCount> qualities_{};
    FaultSettings faults_;
    Parser parser_;
};

} // namespace hd
