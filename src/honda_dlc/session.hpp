#pragma once
#include "application/session.hpp"
#include "honda_dlc/profile.hpp"
#include "honda_dlc/responder.hpp"
#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace hd::dlc {
enum class State { Stopped, Initializing, Polling, Faulted };
struct Settings {
    Time totalMs{200}, interbyteMs{50}, fastMs{100}, coolantMs{1000};
};
struct Stats {
    std::uint64_t accepted{}, timeouts{}, corrupt{}, ignored{};
    double responseHz{};
    std::array<double, ChannelCount> channelHz{};
};
struct Exchange {
    std::string requestHex, responseHex, check, read, formulaSource;
};
// Structurally offline-only: no Transport parameter, serial dependency, or live fallback.
class Session {
  public:
    explicit Session(Settings settings = {}, FreshnessSettings freshness = {});
    void start(Time now);
    void stop(Time now);
    void tick(Time now);
    void setScenario(Scenario scenario);
    void setFaults(Faults faults);
    bool setProfile(std::string_view profile, Time now);
    // All public operations pass the same whitelist before any TX, including test calls.
    bool request(Operation operation, Read read, Time now);
    const Model &model() const { return model_; }
    State state() const { return state_; }
    const Stats &stats() const { return stats_; }
    const std::string &error() const { return error_; }
    std::uint32_t id() const { return session_; }
    const Exchange &lastExchange() const { return exchange_; }
    std::size_t pendingBytes() const { return link_.pendingBytes(); }
    std::function<void(const Sample &)> onSample;
    std::function<void(const RawEvent &)> onRaw;

  private:
    struct Pending {
        Read read;
        std::uint32_t transaction;
        Time deadline;
        std::optional<Time> lastByte;
    };
    void receive(std::span<const std::uint8_t> bytes, Time now);
    void fail(std::string reason, Time now, bool timeout);
    void event(std::string kind, Time now, std::uint32_t transaction, std::string detail,
               std::span<const std::uint8_t> bytes = {});
    void rates(Time now);
    Settings settings_;
    Model model_;
    Parser parser_;
    OfflineLink link_;
    State state_{State::Stopped};
    Stats stats_;
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
};
} // namespace hd::dlc
