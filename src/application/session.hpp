#pragma once
#include "core/model.hpp"
#include "emulator/emulator.hpp"
#include "protocol/protocol.hpp"
#include <functional>
#include <deque>
#include <span>
#include <string>

namespace hd {
enum class SessionState { Stopped, Handshaking, Running, Faulted };
struct SessionSettings { Time pollMs{100}; Time timeoutMs{300}; unsigned helloAttempts{3}; };
struct SessionStats { std::uint64_t accepted{}, timeouts{}, corrupt{}, ignored{}; double responseHz{}; };
struct RawEvent { Time time{}; std::uint32_t session{}, request{}; std::string kind, detail; std::vector<std::uint8_t> bytes; };
class Session {
public:
    explicit Session(SessionSettings settings = {}, FreshnessSettings freshness = {});
    void start(Time now);
    void stop(Time now);
    void tick(Time now);
    Emulator& emulator() { return emulator_; }
    const Model& model() const { return model_; }
    SessionState state() const { return state_; }
    const SessionStats& stats() const { return stats_; }
    std::uint32_t id() const { return sessionId_; }
    std::size_t pendingDeliveries() const { return deliveries_.size(); }
    void receive(std::span<const std::uint8_t> bytes, Time now);
    std::function<void(const Sample&)> onSample;
    std::function<void(const RawEvent&)> onRaw;
private:
    struct Pending { std::uint32_t request{}; std::uint8_t type{}; Time deadline{}; };
    struct Delivery { Time due{}; std::vector<std::uint8_t> bytes; };
    void send(std::uint8_t type, Time now);
    void event(std::string kind, Time now, std::uint32_t request = 0, std::string detail = {}, std::vector<std::uint8_t> bytes = {});
    SessionSettings settings_;
    Model model_;
    Emulator emulator_;
    Parser parser_;
    SessionState state_{SessionState::Stopped};
    SessionStats stats_;
    std::optional<Pending> pending_;
    std::deque<Delivery> deliveries_;
    std::deque<Time> acceptedTimes_;
    std::uint32_t sessionId_{}, nextRequest_{};
    Time nextPoll_{};
    unsigned helloSent_{};
};
}
