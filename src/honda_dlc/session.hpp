#pragma once
#include "application/session.hpp"
#include "honda_dlc/polling.hpp"
#include "honda_dlc/profile.hpp"
#include "honda_dlc/responder.hpp"
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace hd::dlc {
enum class State { Stopped, Initializing, Polling, Faulted };
struct Settings {
    Time totalMs{200}, interbyteMs{50}, fastMs{100}, coolantMs{1000};
    std::optional<PollingPolicy> bridgePolling;
};
inline Settings bridgePollingSettings() {
    Settings settings;
    settings.bridgePolling = PollingPolicy{};
    return settings;
}
struct Stats {
    std::uint64_t accepted{}, timeouts{}, corrupt{}, ignored{};
    double responseHz{};
    std::array<double, ChannelCount> channelHz{};
};
struct Exchange {
    std::string requestHex, responseHex, check, read, formulaSource;
};
// The executor accepts only the profile's narrow byte operations; no serial tunnel.
class Session {
  public:
    explicit Session(Settings settings = {}, FreshnessSettings freshness = {});
    explicit Session(Link &link, Settings settings = {}, FreshnessSettings freshness = {});
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    void start(Time now);
    void stop(Time now);
    void tick(Time now);
    void setScenario(Scenario scenario);
    void setFaults(Faults faults);
    bool setProfile(std::string_view profile, Time now);
    // All public operations pass the same whitelist before any TX, including test calls.
    bool request(Operation operation, Read read, Time now, std::optional<Time> plannedAt = {});
    const Model &model() const { return model_; }
    State state() const { return state_; }
    const Stats &stats() const { return stats_; }
    const TimingMetrics &metrics() const { return metrics_; }
    const std::string &error() const { return error_; }
    std::uint32_t id() const { return session_; }
    const Exchange &lastExchange() const { return exchange_; }
    std::size_t pendingBytes() const { return link_->pendingBytes(); }
    std::function<void(const Sample &)> onSample;
    std::function<void(const RawEvent &)> onRaw;

  private:
    struct Pending {
        Read read;
        std::uint32_t transaction;
        Time deadline;
        Time started;
        std::optional<Time> lastByte;
    };
    void receive(std::span<const std::uint8_t> bytes, Time now, Time measurementAt);
    void fail(std::string reason, Time now, bool timeout);
    void event(std::string kind, Time now, std::uint32_t transaction, std::string detail,
               std::span<const std::uint8_t> bytes = {});
    void rates(Time now);
    Settings settings_;
    Model model_;
    Parser parser_;
    OfflineLink offline_;
    Link *link_{&offline_};
    LabControl *lab_{&offline_};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    State state_{State::Stopped};
    Stats stats_;
    TimingMetrics metrics_;
    BridgeScheduler bridgeScheduler_;
    Exchange exchange_;
    std::string error_;
    bool profileSelected_{true};
    std::optional<Pending> pending_;
    std::array<Time, 3> nextRead_{};
    std::array<std::deque<Time>, ChannelCount> accepted_;
    std::size_t cursor_{};
    std::uint32_t session_{}, transaction_{};
    std::uint64_t generation_{};
    Time initializedAt_{};
    Time ratesStartedAt_{};
};
} // namespace hd::dlc
