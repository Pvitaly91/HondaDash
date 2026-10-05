#pragma once
#include "application/transport.hpp"
#include "protocol/device_extension.hpp"
#include "protocol/protocol.hpp"
#include <deque>
#include <functional>
#include <memory>
namespace hd {
enum class SessionState { Stopped, Opening, BootWaiting, Handshaking, Running, Faulted };
struct SessionSettings {
    Time pollMs{100}, timeoutMs{300};
    unsigned helloAttempts{3};
    Time bootDelayMs{}, handshakeDeadlineMs{5000}, txTimeoutMs{500};
};
struct SessionStats {
    std::uint64_t accepted{}, timeouts{}, corrupt{}, ignored{};
    double responseHz{};
};
// Optional facts from a CRC-validated bridge message. Host and device clocks
// have unrelated epochs; these fields describe durations, never synchronization.
struct BridgeTrace {
    std::uint32_t generation{}, operation{};
    std::uint16_t sequence{}, txElapsedMs{}, rxElapsedMs{}, maxGapMs{};
    std::uint8_t status{};
    std::string origin{"bridge_reported"};
};
struct RawEvent {
    Time time{};
    std::uint32_t session{}, request{};
    std::string kind, detail;
    std::vector<std::uint8_t> bytes;
    std::optional<BridgeTrace> bridge;
};
class Session {
  public:
    using IdGenerator = std::function<std::uint32_t()>;
    explicit Session(Transport &, SessionSettings = {}, FreshnessSettings = {}, IdGenerator = {});
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    void setTransport(Transport &);
    void setSettings(SessionSettings);
    void start(Time);
    void stop(Time);
    void tick(Time);
    void setScenario(std::uint8_t, std::uint32_t);
    void setManual(const std::array<double, ChannelCount> &);
    void setFaults(DeviceFaults);
    void setQualities(const std::array<Quality, ChannelCount> &);
    const Model &model() const { return model_; }
    SessionState state() const { return state_; }
    const SessionStats &stats() const { return stats_; }
    std::uint32_t id() const { return sessionId_; }
    const std::string &error() const { return error_; }
    const std::optional<DeviceInfo> &deviceInfo() const { return info_; }
    std::function<void(const Sample &)> onSample;
    std::function<void(const RawEvent &)> onRaw;

  private:
    struct Pending {
        std::uint32_t request{};
        std::uint8_t type{};
        Time deadline{};
        bool sent{};
        std::uint64_t revision{};
    };
    struct Control {
        std::vector<std::uint8_t> payload;
        std::uint64_t revision{};
        bool dirty{};
    };
    void receive(std::span<const std::uint8_t>, Time);
    void send(std::uint8_t, Time, std::vector<std::uint8_t> = {}, std::uint64_t = 0);
    void opened(Time);
    void transmitted(Time);
    void fail(std::string, Time);
    bool identity(Time);
    void handshake(Time);
    void desired(std::size_t, std::vector<std::uint8_t>);
    void event(std::string, Time, std::uint32_t = 0, std::string = {}, std::vector<std::uint8_t> = {});
    Transport *transport_;
    SessionSettings settings_;
    Model model_;
    Parser parser_;
    IdGenerator ids_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::uint64_t generation_{};
    SessionState state_{SessionState::Stopped};
    SessionStats stats_;
    std::optional<Pending> pending_;
    std::optional<DeviceInfo> info_;
    std::array<Control, 4> controls_{};
    std::deque<Time> acceptedTimes_;
    std::string error_;
    std::uint32_t sessionId_{}, nextRequest_{};
    Time nextPoll_{}, bootUntil_{}, handshakeUntil_{};
    unsigned helloSent_{};
    bool helloOk_{}, controlLast_{};
};
} // namespace hd
