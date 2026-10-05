#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace hd {

using Time = std::uint64_t; // Monotonic milliseconds, supplied by the caller.

enum class Channel : std::uint8_t { Rpm, Speed, Coolant, Intake, Throttle, Map, Voltage };
inline constexpr std::size_t ChannelCount = 7;
constexpr std::size_t channelIndex(Channel channel) { return static_cast<std::size_t>(channel); }

enum class Quality : std::uint8_t { NoData, Valid, Stale, Unsupported, Invalid };
const char* qualityName(Quality quality);
// Widget ranges belong to the synthetic demo. A protocol decoder with its own
// documented domain must not be constrained by those display ranges.
enum class RangePolicy : std::uint8_t { DemoLimits, DecoderValidated };
inline constexpr std::uint8_t AllChannelsMask = (1u << ChannelCount) - 1u;

struct ChannelInfo {
    const char* name;
    const char* unit;
    double min;
    double max;
    int decimals;
};
const ChannelInfo& channelInfo(Channel channel);

struct FreshnessSettings {
    Time staleMs{1000};
    Time hideMs{3000};
};

struct Measurement {
    std::optional<double> value;
    Quality quality{Quality::NoData};
    std::optional<Time> lastValid;
    std::uint32_t session{};
    std::uint32_t request{};
    std::string source{"synthetic-demo-v1"};
    std::string reason;
};

struct Sample {
    Time time{};
    std::uint32_t session{};
    std::uint32_t request{};
    std::array<std::optional<double>, ChannelCount> values{};
    std::array<Quality, ChannelCount> qualities{};
    std::uint8_t updatedMask{AllChannelsMask};
    std::string source{"synthetic-demo-v1"};
    std::array<std::string, ChannelCount> reasons{};
    RangePolicy rangePolicy{RangePolicy::DemoLimits};
    // Optional host-clock lower bound of acquisition time. time remains the
    // actual host receipt time; a delayed bridge result must not appear new.
    std::optional<Time> freshnessSince;
};

class Model {
public:
    explicit Model(FreshnessSettings settings = {});
    void apply(const Sample& sample);
    void refresh(Time now);
    void reset();
    const std::array<Measurement, ChannelCount>& channels() const { return channels_; }
    std::optional<double> current(Channel channel, Time now) const;

private:
    FreshnessSettings settings_;
    std::array<Measurement, ChannelCount> channels_{};
};

} // namespace hd
