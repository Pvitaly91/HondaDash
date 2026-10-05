#pragma once
#include "application/transport.hpp"
#include "honda_dlc/link.hpp"
#include "protocol/protocol.hpp"
#include <memory>
#include <optional>

namespace hd::bridge {
enum class State {
    Disconnected,
    Opening,
    BootWaiting,
    Handshaking,
    Ready,
    Starting,
    Initializing,
    Running,
    Faulted
};
struct Settings {
    Time bootMs{}, txMs{500}, handshakeMs{2000}, acceptMs{1000}, resultMs{1500}, maxResultAgeMs{1200},
        openMs{2000};
};
struct Info {
    std::string identity;
    std::uint8_t protocolVersion{}, policyVersion{}, backend{};
    bool physicalDlcEnabled{};
    std::uint16_t capabilities{};
    std::uint32_t generation{};
    std::string firmware;
};
struct Diagnostics {
    std::uint32_t generation{}, operation{};
    std::uint8_t status{};
    std::uint16_t txElapsedMs{}, rxElapsedMs{}, maxGapMs{};
    std::string summary;
};
// Host USB operation watchdogs are independent of the embedded DLC engine clock.
class Client final : public dlc::Link, public dlc::LabControl {
  public:
    explicit Client(Transport &transport, Settings settings = {});
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    void connect(Time now); // Handshake only: never starts DLC by itself.
    void disconnect(Time now);
    void start(std::uint32_t session, Time now, dlc::LinkCallbacks callbacks) override;
    bool execute(std::span<const std::uint8_t>, dlc::Read, std::uint32_t transaction, Time now) override;
    void abort(Time now) override;
    void tick(Time now) override;
    void setScenario(dlc::Scenario scenario) override;
    void setFaults(dlc::Faults faults) override;
    std::size_t pendingBytes() const override { return transport_.pendingBytes() + parser_.buffered(); }
    bool deviceTimed() const override { return true; }
    bool canExecute() const override;
    State state() const { return state_; }
    const char *stateName() const;
    const std::optional<Info> &info() const { return info_; }
    const std::string &error() const { return error_; }
    const Diagnostics &diagnostics() const { return diagnostics_; }
    std::function<void(const RawEvent &)> onRaw;

  private:
    struct Pending {
        std::uint8_t command{};
        std::uint32_t id{}, transaction{};
        Time started{}, sentAt{};
        bool sent{};
        dlc::Read read{};
        std::vector<std::uint8_t> dlcBytes;
    };
    void command(std::uint8_t command, std::vector<std::uint8_t> payload, Time now,
                 std::uint32_t transaction = 0, dlc::Read read = {},
                 std::span<const std::uint8_t> bytes = {});
    void receive(std::span<const std::uint8_t>, Time now);
    void frame(const Frame &, Time now);
    void fail(std::string reason, Time now, bool timeout = false);
    void configure(Time now);
    void boundary(Time now);
    void emitRaw(std::string kind, Time now, std::string detail, std::span<const std::uint8_t> bytes = {},
                 std::optional<BridgeTrace> trace = {}, bool associated = true);
    Transport &transport_;
    Settings settings_;
    Parser parser_;
    dlc::LinkCallbacks callbacks_;
    State state_{State::Disconnected};
    std::optional<Info> info_;
    std::optional<Pending> pending_;
    Diagnostics diagnostics_;
    dlc::Scenario scenario_{dlc::Scenario::Baseline};
    dlc::Faults faults_;
    std::string error_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::uint64_t epoch_{};
    std::uint32_t session_{}, nextId_{}, modelSession_{}, configRevision_{}, sentConfigRevision_{};
    std::uint16_t eventSequence_{};
    Time bootAt_{}, openingDeadline_{}, now_{};
    bool startRequested_{}, configDirty_{true};
};
} // namespace hd::bridge
