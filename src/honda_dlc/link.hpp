#pragma once
#include "application/session.hpp"
#include "honda_dlc/protocol.hpp"
#include <functional>
#include <span>

namespace hd::dlc {
enum class Scenario { Baseline, Higher, Changing, Boundary };
const char *scenarioId(Scenario scenario);
struct Faults {
    bool silent{};
    Time delayMs{};
    bool corruptNext{}, wrongLengthNext{}, truncateNext{}, noiseNext{};
    Time gapMs{};
    bool trailingNext{}, headerNext{};
};
// Laboratory controls are not part of a future physical DLC executor contract.
class LabControl {
  public:
    virtual ~LabControl() = default;
    virtual void setScenario(Scenario) = 0;
    virtual void setFaults(Faults) = 0;
};
struct LinkCallbacks {
    std::function<void(Time)> ready;
    // Measurement time is a conservative host-clock lower bound, never device millis().
    std::function<void(std::span<const std::uint8_t>, Time, Time)> received;
    std::function<void(std::string, Time, bool)> fault;
    std::function<void(const RawEvent &)> raw;
};
class Link {
  public:
    virtual ~Link() = default;
    virtual void start(std::uint32_t session, Time now, LinkCallbacks callbacks) = 0;
    virtual bool execute(std::span<const std::uint8_t>, Read, std::uint32_t transaction, Time now) = 0;
    virtual void abort(Time now) = 0;
    virtual void tick(Time now) = 0;
    virtual std::size_t pendingBytes() const = 0;
    virtual bool deviceTimed() const = 0;
    virtual bool canExecute() const { return true; }
};
} // namespace hd::dlc
