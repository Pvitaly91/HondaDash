#include "honda_dlc/profile.hpp"
#include <algorithm>
#include <cmath>

namespace hd::dlc {
const std::array<Field, 3> &fields() {
    static const std::array<Field, 3> result{
        {{Channel::Rpm,
          {0x00, 2},
          "RPM / 0x00 + 2",
          "1875000 / (BE16 + 1); integer division",
          "kerpz ArduinoHondaOBD@8e990713628a28197eda2bdf094662ca0c86b98a / hobd_uni2/hobd_uni2.ino:257-269 "
          "readEcuData"},
         {Channel::Throttle,
          {0x14, 1},
          "TPS / 0x14 + 1",
          "(raw - 24) / 2; signed integer division",
          "kerpz ArduinoHondaOBD@8e990713628a28197eda2bdf094662ca0c86b98a / hobd_uni2/hobd_uni2.ino:285-293; "
          "domain policy 24..224"},
         {Channel::Coolant,
          {0x10, 1},
          "ECT / 0x10 + 1",
          "degree-5 polynomial; truncate toward zero",
          "kerpz ArduinoHondaOBD@8e990713628a28197eda2bdf094662ca0c86b98a / hobd_uni2/hobd_uni2.ino:285-288; "
          "calibration unverified"}}};
    return result;
}
Sample unavailable(Time time, std::uint32_t session) {
    Sample sample;
    sample.time = time;
    sample.session = session;
    sample.source = ProfileId;
    sample.rangePolicy = RangePolicy::DecoderValidated;
    sample.qualities.fill(Quality::Unsupported);
    sample.reasons.fill("Не визначено для цього профілю");
    for (const auto &field : fields()) {
        const auto index = channelIndex(field.channel);
        sample.qualities[index] = Quality::NoData;
        sample.reasons[index] = "Очікування відповіді; застосовність до ECU не перевірено";
    }
    return sample;
}
std::optional<Sample> decode(Read read, std::span<const std::uint8_t> payload, Time time,
                             std::uint32_t session, std::uint32_t transaction) {
    if (!allowed(read) || payload.size() != read.length)
        return std::nullopt;
    const auto found = std::find_if(fields().begin(), fields().end(), [read](const Field &field) {
        return field.read.address == read.address && field.read.length == read.length;
    });
    if (found == fields().end())
        return std::nullopt;
    Sample sample;
    sample.time = time;
    sample.session = session;
    sample.request = transaction; // Host metadata only. The response contains neither ID nor address.
    sample.source = ProfileId;
    sample.rangePolicy = RangePolicy::DecoderValidated;
    const auto index = channelIndex(found->channel);
    sample.updatedMask = static_cast<std::uint8_t>(1u << index);
    sample.qualities[index] = Quality::Valid;
    sample.reasons[index] = "Reference-derived; hardware_verified=false; calibration unverified";
    if (found->channel == Channel::Rpm) {
        const auto word = (static_cast<std::uint32_t>(payload[0]) << 8) | payload[1];
        if (word == 65535) {
            sample.qualities[index] = Quality::Invalid;
            sample.reasons[index] = "raw FFFF: sentinel/область формули не підтверджені; не означає 0 RPM";
        } else {
            sample.values[index] = static_cast<double>(1875000u / (word + 1u));
        }
    } else if (found->channel == Channel::Throttle) {
        const auto raw = static_cast<int>(payload[0]);
        if (raw < 24 || raw > 224) {
            sample.qualities[index] = Quality::Invalid;
            sample.reasons[index] = "Поза консервативною областю TPS raw 24..224 (політика HondaDash)";
        } else
            sample.values[index] = static_cast<double>((raw - 24) / 2);
    } else {
        const double raw = payload[0];
        // Own evaluation of the published coefficients, deliberately in desktop double.
        const double value = 155.04149 - 3.0414878 * raw + 0.03952185 * std::pow(raw, 2) -
                             0.00029383913 * std::pow(raw, 3) + 0.0000010792568 * std::pow(raw, 4) -
                             0.0000000015618437 * std::pow(raw, 5);
        sample.values[index] = std::trunc(value);
    }
    return sample;
}
} // namespace hd::dlc
