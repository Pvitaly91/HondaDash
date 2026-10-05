#include "protocol/device_extension.hpp"
#include <cmath>
#include <stdexcept>
namespace hd {
std::optional<DeviceInfo> decodeDeviceInfo(std::span<const std::uint8_t> p) {
    if (p.size() < 8 || p[0] != 1 || (p[1] != 1 && p[1] != 2) || p[2] != 0 || p[3] != 2)
        return {};
    std::string endpoint(p.begin() + 7, p.end());
    if (endpoint != (p[1] == 1 ? "desktop-emulator" : "nano-synthetic"))
        return {};
    return DeviceInfo{endpoint,
                      std::to_string(p[2]) + "." + std::to_string(p[3]) + "." + std::to_string(p[4]),
                      static_cast<std::uint16_t>(p[5] | (std::uint16_t(p[6]) << 8)), p[1]};
}
std::vector<std::uint8_t> scenarioPayload(std::uint8_t s, std::uint32_t seed) {
    if (s > 3)
        throw std::invalid_argument("Unknown scenario");
    std::vector<std::uint8_t> p{s};
    for (unsigned i = 0; i < 4; ++i)
        p.push_back(static_cast<std::uint8_t>(seed >> (8 * i)));
    return p;
}
std::vector<std::uint8_t> manualPayload(const std::array<double, ChannelCount> &v) {
    constexpr double scales[]{1, 10, 10, 10, 10, 10, 100};
    std::vector<std::uint8_t> p;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const auto &c = channelInfo(static_cast<Channel>(i));
        if (!std::isfinite(v[i]) || v[i] < c.min || v[i] > c.max)
            throw std::invalid_argument("Manual value outside range");
        auto word = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v[i] * scales[i])));
        p.push_back(static_cast<std::uint8_t>(word));
        p.push_back(static_cast<std::uint8_t>(word >> 8));
    }
    return p;
}
std::vector<std::uint8_t> faultsPayload(DeviceFaults f) {
    if (f.delayMs > 5000)
        throw std::invalid_argument("Delay exceeds 5000ms");
    return {static_cast<std::uint8_t>(f.silent), static_cast<std::uint8_t>(f.delayMs),
            static_cast<std::uint8_t>(f.delayMs >> 8),
            static_cast<std::uint8_t>((f.corruptNext ? 1 : 0) | (f.truncateNext ? 2 : 0))};
}
} // namespace hd
