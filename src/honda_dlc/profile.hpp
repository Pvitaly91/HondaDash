#pragma once
#include "core/model.hpp"
#include "honda_dlc/protocol.hpp"
#include <string_view>
namespace hd::dlc {
inline constexpr std::string_view ProfileId = "honda-dlc-kerpz-obd1-reference-v1";
inline constexpr unsigned ProfileVersion = 1;
inline constexpr bool HardwareVerified = false, LiveEnabled = false;
struct Field {
    Channel channel;
    Read read;
    const char *name;
    const char *formula;
    const char *evidence;
};
const std::array<Field, 3> &fields();
Sample unavailable(Time time, std::uint32_t session);
std::optional<Sample> decode(Read read, std::span<const std::uint8_t> payload, Time time,
                             std::uint32_t session, std::uint32_t transaction);
} // namespace hd::dlc
