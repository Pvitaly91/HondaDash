#pragma once
#include "bridge/client.hpp"

namespace hd::bench {
bool distinctPorts(std::string_view first, std::string_view second);
enum class State {
    Disconnected,
    Handshaking,
    Ready,
    Quiescing,
    Draining,
    Preparing,
    Arming,
    Running,
    Faulted
};
// Owns two USB control paths. Measurement bytes can enter Session only through
// the bridge Client; responder USB never supplies decoded or raw measurements.
class Controller final : public dlc::Link, public dlc::LabControl {
  public:
    Controller(Transport &bridge, Transport &responder, bridge::Settings settings = {});
    ~Controller();
    void connect(Time now); // Both HELLOs only. No line TX, NEW, ARM or CONFIG.
    void disconnect(Time now);
    void start(std::uint32_t session, Time now, dlc::LinkCallbacks callbacks) override;
    void abort(Time now) override;
    void tick(Time now) override;
    bool execute(std::span<const std::uint8_t>, dlc::Read, std::uint32_t, Time) override;
    bool canExecute() const override;
    bool deviceTimed() const override { return true; }
    std::size_t pendingBytes() const override;
    void setScenario(dlc::Scenario) override;
    void setFaults(dlc::Faults) override;
    State state() const { return state_; }
    const char *stateName() const;
    const std::string &error() const { return error_; }
    const bridge::Client &bridgeClient() const { return bridge_; }
    const std::optional<bridge::Info> &responderInfo() const { return responderInfo_; }
    const std::vector<std::uint8_t> &responderDiagnostics() const { return responderDiagnostics_; }
    bool quiescent() const {
        return quiescent_ && !collectDiagnostics_ && !pending_ && !bridge_.operationPending();
    }
    std::function<void(const RawEvent &)> onRaw;

  private:
    struct Pending {
        std::uint8_t command;
        std::uint32_t id;
        Time started, sentAt;
        bool sent{};
    };
    void receive(std::span<const std::uint8_t>, Time);
    void frame(const Frame &, Time);
    void command(std::uint8_t, std::vector<std::uint8_t>, Time);
    void fail(std::string, Time, bool timeout = false);
    void raw(std::string, Time, std::string, std::span<const std::uint8_t> = {});
    void forward(const RawEvent &);
    void beginQuiesce(Time);
    void configure(Time);
    void activate(Time);
    bridge::Client bridge_;
    Transport &responder_;
    bridge::Settings settings_;
    Parser parser_;
    dlc::LinkCallbacks callbacks_;
    std::optional<bridge::Info> responderInfo_;
    std::optional<Pending> pending_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::uint64_t epoch_{};
    std::uint32_t usbSession_{}, nextId_{}, modelSession_{}, configRevision_{}, sentConfigRevision_{};
    Time now_{}, bootAt_{}, openDeadline_{}, phaseAt_{};
    State state_{State::Disconnected};
    std::string error_;
    dlc::Scenario scenario_{dlc::Scenario::Baseline};
    dlc::Faults faults_;
    bool opened_{}, helloSent_{}, startRequested_{}, stopping_{}, quiescent_{}, configDirty_{true};
    std::vector<std::uint8_t> responderDiagnostics_;
    bool collectDiagnostics_{};
};
} // namespace hd::bench
