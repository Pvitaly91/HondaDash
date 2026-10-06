#pragma once
#include "core/model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace hd::dlc {
inline constexpr char BridgeSchedulerPolicy[] = "bridge-weighted-cycle-v1";
inline constexpr char BridgeFreshnessPolicy[] = "bridge-bounded-age-v1";
inline constexpr unsigned BridgePolicyVersion = 1;
inline constexpr unsigned BridgeSchedulerVersion = BridgePolicyVersion;
inline constexpr unsigned BridgeFreshnessVersion = BridgePolicyVersion;
inline constexpr Time BridgeSlotMs = 320, BridgeResultBudgetMs = 320, BridgeReserveMs = 100;
inline constexpr Time BridgeMaximumTickMs = 20, BridgeMaximumUsbDelayMs = 50;
inline constexpr std::array<std::size_t, 5> BridgeCycle{0, 1, 2, 0, 1}; // profile order RPM/TPS/ECT
constexpr Time bridgeMaximumRequestInterval(Channel channel) {
    return channel == Channel::Coolant                               ? 1600
           : channel == Channel::Rpm || channel == Channel::Throttle ? 960
                                                                     : 0;
}
constexpr std::array<Time, ChannelCount> bridgeRequestedIntervals() {
    return {800, 0, 1600, 0, 800, 0, 0};
}
inline FreshnessSettings bridgeFreshness() {
    FreshnessSettings settings;
    settings.channels[channelIndex(Channel::Rpm)] = FreshnessThresholds{1400, 4200};
    settings.channels[channelIndex(Channel::Throttle)] = FreshnessThresholds{1400, 4200};
    settings.channels[channelIndex(Channel::Coolant)] = FreshnessThresholds{2100, 6300};
    return settings;
}
struct PollingPolicy {
    Time slotMs{BridgeSlotMs};
    // Mean desired intervals in profile order RPM/TPS/ECT. They describe goals,
    // not queued jobs: lowering them cannot bypass the fixed service budget.
    std::array<Time, 3> requestedIntervalsMs{800, 800, 1600};
};
class BridgeScheduler {
  public:
    void reset(Time now, PollingPolicy policy) {
        policy_ = policy;
        due_ = now;
        cursor_ = 0;
    }
    std::optional<std::size_t> selected(Time now) const {
        return now >= due_ ? std::optional<std::size_t>(BridgeCycle[cursor_]) : std::nullopt;
    }
    Time due() const { return due_; }
    void submitted(Time now) {
        cursor_ = (cursor_ + 1) % BridgeCycle.size();
        due_ = now + policy_.slotMs; // No historical debt or catch-up burst.
    }

  private:
    PollingPolicy policy_;
    Time due_{};
    std::size_t cursor_{};
};
struct DistributionSummary {
    std::uint64_t observations{};
    std::size_t n{};
    std::optional<Time> min, median, p95, max;
};
class BoundedDistribution {
  public:
    static constexpr std::size_t Capacity = 256;
    void add(Time value) {
        values_[next_] = value;
        next_ = (next_ + 1) % Capacity;
        size_ = std::min(size_ + 1, Capacity);
        ++observations_;
    }
    DistributionSummary summary() const {
        DistributionSummary result;
        result.observations = observations_;
        result.n = size_;
        if (!size_)
            return result;
        auto sorted = values_;
        std::sort(sorted.begin(), sorted.begin() + size_);
        result.min = sorted[0];
        result.median = sorted[(size_ - 1) / 2];
        result.p95 = sorted[(size_ * 95 + 99) / 100 - 1];
        result.max = sorted[size_ - 1];
        return result;
    }

  private:
    std::array<Time, Capacity> values_{};
    std::size_t next_{}, size_{};
    std::uint64_t observations_{};
};
struct ChannelTiming {
    Time requestedIntervalMs{};
    Time maximumExpectedIntervalMs{};
    std::uint64_t submitted{}, accepted{}, validDecoded{}, missedRequestedPeriods{}, staleEvents{},
        staleDurationMs{}, hiddenEvents{};
    Time maximumAgeMs{};
    std::optional<Time> lastStart, lastUpdate, lastUpdateIntervalMs, lastRequestToResultMs,
        lastModelUpdateDelayMs;
    BoundedDistribution requestIntervals, updateIntervals, ageAtReceipt, scheduleDelay;
    Quality previousQuality{Quality::NoData};
    bool previouslyVisible{};
};
struct TimingMetrics {
    std::array<ChannelTiming, ChannelCount> channels;
    BoundedDistribution requestToResult, firmwareOperation;
    std::size_t maximumPendingBytes{};
    std::uint64_t submitted{}, rejectedSubmissions{};
    std::optional<Time> lastObserved;
    void submittedRead(Channel channel, Time now, Time planned) {
        auto &c = channels[channelIndex(channel)];
        ++submitted;
        ++c.submitted;
        if (c.lastStart) {
            const auto interval = now - *c.lastStart;
            c.requestIntervals.add(interval);
            const auto limit =
                c.maximumExpectedIntervalMs ? c.maximumExpectedIntervalMs : c.requestedIntervalMs;
            if (limit && interval > limit)
                c.missedRequestedPeriods += (interval - 1) / limit;
        }
        c.lastStart = now;
        c.scheduleDelay.add(now >= planned ? now - planned : 0);
    }
    void acceptedSample(const Sample &sample, Time requestStart) {
        requestToResult.add(sample.time - requestStart);
        for (std::size_t i = 0; i < ChannelCount; ++i) {
            if (!(sample.updatedMask & (1u << i)))
                continue;
            auto &c = channels[i];
            ++c.accepted;
            if (sample.qualities[i] == Quality::Valid)
                ++c.validDecoded;
            if (c.lastUpdate) {
                c.lastUpdateIntervalMs = sample.time - *c.lastUpdate;
                c.updateIntervals.add(*c.lastUpdateIntervalMs);
            }
            c.lastUpdate = sample.time;
            c.lastRequestToResultMs = sample.time - requestStart;
            c.lastModelUpdateDelayMs = 0; // apply is synchronous at host receipt; no device inference.
            c.ageAtReceipt.add(sample.time - sample.freshnessSince.value_or(sample.time));
        }
    }
    void observe(const Model &model, Time now, std::size_t pendingBytes) {
        maximumPendingBytes = std::max(maximumPendingBytes, pendingBytes);
        for (std::size_t i = 0; i < ChannelCount; ++i) {
            auto &c = channels[i];
            const auto &m = model.channels()[i];
            const auto channel = static_cast<Channel>(i);
            if (m.lastValid && (m.quality == Quality::Valid || m.quality == Quality::Stale) &&
                now >= *m.lastValid) {
                c.maximumAgeMs = std::max(c.maximumAgeMs, now - *m.lastValid);
                if (lastObserved && now > *lastObserved) {
                    const auto threshold = *m.lastValid + model.freshness().effective(channel).staleMs + 1;
                    const auto from = std::max(*lastObserved, threshold);
                    if (now > from)
                        c.staleDurationMs += now - from;
                }
            }
            if (m.quality == Quality::Stale && c.previousQuality != Quality::Stale)
                ++c.staleEvents;
            const bool visible = model.current(channel, now).has_value();
            if (!visible && c.previouslyVisible)
                ++c.hiddenEvents;
            c.previouslyVisible = visible;
            c.previousQuality = m.quality;
        }
        lastObserved = now;
    }
};
} // namespace hd::dlc
