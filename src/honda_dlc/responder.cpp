#include "honda_dlc/responder.hpp"
#include <algorithm>
#include <array>

namespace hd::dlc {
const char *scenarioId(Scenario scenario) {
    switch (scenario) {
    case Scenario::Baseline:
        return "raw-baseline-09c3-40-58";
    case Scenario::Higher:
        return "raw-higher-04e1-20-ae";
    case Scenario::Changing:
        return "raw-alternating-sets";
    case Scenario::Boundary:
        return "raw-boundary-ffff-ff-18";
    }
    return "unknown";
}
namespace {
template <std::size_t N>
bool is(std::span<const std::uint8_t> bytes, const std::array<std::uint8_t, N> &expected) {
    return bytes.size() == expected.size() && std::equal(bytes.begin(), bytes.end(), expected.begin());
}
} // namespace
void ScriptedHondaEcu::reset() {
    initialized_ = false;
    reads_ = rejected_ = 0;
    readyAt_ = 0;
}
std::vector<std::uint8_t> ScriptedHondaEcu::receive(std::span<const std::uint8_t> request, Time now) {
    static constexpr std::array<std::uint8_t, 11> wake{0x68, 0x6a, 0xf5, 0xaf, 0xbf, 0xb3,
                                                       0xb2, 0xc1, 0xdb, 0xb3, 0xe9};
    static constexpr std::array<std::uint8_t, 5> rpm{0x20, 0x05, 0x00, 0x02, 0xd9};
    static constexpr std::array<std::uint8_t, 5> ect{0x20, 0x05, 0x10, 0x01, 0xca};
    static constexpr std::array<std::uint8_t, 5> tps{0x20, 0x05, 0x14, 0x01, 0xc6};
    if (is(request, wake)) {
        initialized_ = true;
        readyAt_ = now + 300;
        return {};
    }
    if (!initialized_ || now < readyAt_ || (!is(request, rpm) && !is(request, ect) && !is(request, tps))) {
        ++rejected_;
        return {};
    }
    const auto selected = scenario_ == Scenario::Changing
                              ? ((reads_ / 3) % 2 ? Scenario::Higher : Scenario::Baseline)
                              : scenario_;
    ++reads_;
    if (is(request, rpm)) {
        if (selected == Scenario::Higher)
            return {0x00, 0x05, 0x04, 0xe1, 0x16};
        if (selected == Scenario::Boundary)
            return {0x00, 0x05, 0xff, 0xff, 0xfd};
        return {0x00, 0x05, 0x09, 0xc3, 0x2f};
    }
    if (is(request, ect)) {
        if (selected == Scenario::Higher)
            return {0x00, 0x04, 0x20, 0xdc};
        if (selected == Scenario::Boundary)
            return {0x00, 0x04, 0xff, 0xfd};
        return {0x00, 0x04, 0x40, 0xbc};
    }
    if (selected == Scenario::Higher)
        return {0x00, 0x04, 0xae, 0x4e};
    if (selected == Scenario::Boundary)
        return {0x00, 0x04, 0x18, 0xe4};
    return {0x00, 0x04, 0x58, 0xa4};
}
void OfflineLink::newExperiment() {
    ++generation_;
    deliveries_.clear();
    ecu_.reset();
}
bool OfflineLink::send(std::span<const std::uint8_t> request, Time now) {
    auto response = ecu_.receive(request, now);
    if (response.empty())
        return true; // Initialization intentionally has no invented ACK.
    const auto faults = faults_;
    faults_.corruptNext = faults_.wrongLengthNext = faults_.truncateNext = faults_.noiseNext = false;
    if (faults.silent)
        return true;
    if (faults.corruptNext)
        response.back() ^= 1;
    if (faults.wrongLengthNext)
        ++response[1];
    if (faults.truncateNext)
        response.resize(response.size() - 2);
    if (faults.noiseNext)
        response.insert(response.begin(), {0x7e, 0x55});
    if (deliveries_.size() + response.size() > Capacity)
        return false;
    Time due = now + 2 + std::min<Time>(faults.delayMs, 10000);
    if (!deliveries_.empty())
        due = std::max(due, deliveries_.back().due + 2);
    for (auto byte : response) {
        deliveries_.push_back({due, byte});
        due += 2;
    }
    return true;
}
void OfflineLink::tick(Time now, const std::function<void(std::span<const std::uint8_t>, Time)> &receive) {
    const auto generation = generation_;
    // At most Capacity callbacks; reset of a parser elsewhere cannot delete this queue.
    while (!deliveries_.empty() && deliveries_.front().due <= now) {
        const auto byte = deliveries_.front().value;
        deliveries_.pop_front();
        receive(std::span(&byte, 1), now);
        if (generation != generation_)
            return;
    }
}
} // namespace hd::dlc
