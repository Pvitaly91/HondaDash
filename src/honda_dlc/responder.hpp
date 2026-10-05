#pragma once
#include "core/model.hpp"
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace hd::dlc {
enum class Scenario { Baseline, Higher, Changing, Boundary };
const char *scenarioId(Scenario scenario);
struct Faults {
    bool silent{};
    Time delayMs{};
    bool corruptNext{}, wrongLengthNext{}, truncateNext{}, noiseNext{};
};
// Hand-constructed reference-derived fixtures, not captures from a physical ECU.
// The responder does not call the production encoder, parser, or profile decoder.
class ScriptedHondaEcu {
  public:
    std::vector<std::uint8_t> receive(std::span<const std::uint8_t> request, Time now);
    void reset();
    void setScenario(Scenario scenario) {
        scenario_ = scenario;
        reads_ = 0;
    }
    bool initialized() const { return initialized_; }
    std::uint64_t rejected() const { return rejected_; }

  private:
    Scenario scenario_{Scenario::Baseline};
    bool initialized_{};
    Time readyAt_{};
    std::uint64_t reads_{}, rejected_{};
};
class OfflineLink {
  public:
    static constexpr std::size_t Capacity = 64;
    void newExperiment(); // Explicit replacement of the simulated ECU, never a physical recovery claim.
    void setScenario(Scenario scenario) { ecu_.setScenario(scenario); }
    void setFaults(Faults faults) { faults_ = faults; }
    bool send(std::span<const std::uint8_t> request, Time now);
    void tick(Time now, const std::function<void(std::span<const std::uint8_t>, Time)> &receive);
    std::size_t pendingBytes() const { return deliveries_.size(); }

  private:
    struct Byte {
        Time due;
        std::uint8_t value;
    };
    ScriptedHondaEcu ecu_;
    Faults faults_;
    std::deque<Byte> deliveries_;
    std::uint64_t generation_{};
};
} // namespace hd::dlc
