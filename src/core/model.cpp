#include "core/model.hpp"

#include <cmath>
#include <stdexcept>

namespace hd {
namespace {
constexpr std::array<ChannelInfo, ChannelCount> info{{
    {"rpm", "rpm", 0.0, 12000.0, 0},
    {"speed", "km/h", 0.0, 250.0, 1},
    {"coolant", "degC", -40.0, 150.0, 1},
    {"intake", "degC", -40.0, 120.0, 1},
    {"throttle", "%", 0.0, 100.0, 1},
    {"map", "kPa", 0.0, 250.0, 1},
    {"voltage", "V", 0.0, 20.0, 2}
}};
Time age(Time now, Time then) { return now >= then ? now - then : 0; }
}

const ChannelInfo& channelInfo(Channel channel) { return info.at(channelIndex(channel)); }

const char* qualityName(Quality quality) {
    switch (quality) {
    case Quality::NoData: return "NoData";
    case Quality::Valid: return "Valid";
    case Quality::Stale: return "Stale";
    case Quality::Unsupported: return "Unsupported";
    case Quality::Invalid: return "Invalid";
    }
    return "Invalid";
}

Model::Model(FreshnessSettings settings) : settings_(settings) {
    if (settings_.hideMs < settings_.staleMs)
        throw std::invalid_argument("hideMs must be >= staleMs");
}

void Model::apply(const Sample& sample) {
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        if ((sample.updatedMask & (1u << i)) == 0) continue;
        auto& measurement = channels_[i];
        measurement.session = sample.session;
        measurement.request = sample.request;
        measurement.source = sample.source;
        measurement.reason = sample.reasons[i];
        const auto quality = sample.qualities[i];
        if (quality == Quality::Valid && sample.values[i] && std::isfinite(*sample.values[i]) &&
            (sample.rangePolicy == RangePolicy::DecoderValidated ||
             (*sample.values[i] >= info[i].min && *sample.values[i] <= info[i].max))) {
            measurement.value = sample.values[i];
            measurement.quality = Quality::Valid;
            measurement.lastValid = sample.time;
        } else {
            measurement.value.reset();
            measurement.quality = quality == Quality::Unsupported ? Quality::Unsupported :
                quality == Quality::NoData ? Quality::NoData : Quality::Invalid;
            // An unavailable entry does not renew the timestamp of a valid measurement.
        }
    }
}

void Model::refresh(Time now) {
    for (auto& measurement : channels_) {
        if ((measurement.quality == Quality::Valid || measurement.quality == Quality::Stale) &&
            measurement.lastValid)
            measurement.quality = age(now, *measurement.lastValid) > settings_.staleMs
                ? Quality::Stale : Quality::Valid;
    }
}

std::optional<double> Model::current(Channel channel, Time now) const {
    const auto& measurement = channels_.at(channelIndex(channel));
    if ((measurement.quality != Quality::Valid && measurement.quality != Quality::Stale) ||
        !measurement.lastValid || age(now, *measurement.lastValid) > settings_.hideMs)
        return std::nullopt;
    return measurement.value;
}

void Model::reset() { channels_ = {}; }

} // namespace hd
