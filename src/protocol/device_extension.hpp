#pragma once
#include "protocol/protocol.hpp"
#include <array>
#include <string>
namespace hd {
inline constexpr std::uint8_t GetDeviceInfo = 0x10, SetScenario = 0x11, SetManual = 0x12, SetFaults = 0x13,
                              SetQuality = 0x14;
inline constexpr std::uint16_t CapabilityScenario = 1, CapabilityManual = 2, CapabilityFaults = 4,
                               CapabilityQuality = 8;
struct DeviceInfo {
    std::string endpoint, firmware;
    std::uint16_t capabilities{};
    std::uint8_t kind{};
};
struct DeviceFaults {
    bool silent{};
    std::uint16_t delayMs{};
    bool corruptNext{}, truncateNext{};
};
std::optional<DeviceInfo> decodeDeviceInfo(std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> scenarioPayload(std::uint8_t scenario, std::uint32_t seed);
std::vector<std::uint8_t> manualPayload(const std::array<double, ChannelCount> &values);
std::vector<std::uint8_t> faultsPayload(DeviceFaults faults);
} // namespace hd
