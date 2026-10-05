#include "bridge/native_transport.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace hd::bridge {
NativeTransport::NativeTransport(NativeOptions options) : options_(options) {
    if (!options.fragmentSize || options.fragmentSize > 79)
        throw std::invalid_argument("invalid native bridge USB fragment size");
}
std::uint32_t NativeTransport::deviceTime(Time now) const {
    return options_.deviceClockOffset + static_cast<std::uint32_t>(now);
}
void NativeTransport::resetDevice(Time now) {
    endpoint_.reset(deviceTime(now));
}
void NativeTransport::start(TransportCallbacks callbacks, Time now) {
    close();
    callbacks_ = std::move(callbacks);
    open_ = true;
    // Reopening host USB is not a hidden NEW_EXPERIMENT: the same endpoint and
    // pending virtual DLC bytes survive. Only explicit resetDevice/New clear it.
    if (callbacks_.opened)
        callbacks_.opened(now);
}
void NativeTransport::close() {
    ++generation_;
    open_ = false;
    deliveries_.clear(); // USB transport is closed; does not pretend this is a DLC parser reset.
    queuedBytes_ = 0;
    callbacks_ = {};
}
bool NativeTransport::send(std::span<const std::uint8_t> bytes, Time now) {
    if (!open_ || bytes.size() > 79)
        return false;
    const auto generation = generation_;
    if (callbacks_.raw)
        callbacks_.raw("TX", bytes, now);
    if (generation != generation_ || !open_)
        return false;
    for (auto byte : bytes)
        endpoint_.receive(byte, deviceTime(now));
    if (callbacks_.sent)
        callbacks_.sent(now);
    return true;
}
void NativeTransport::tick(Time now) {
    if (!open_)
        return;
    const auto generation = generation_;
    endpoint_.tick(deviceTime(now));
    std::array<std::uint8_t, 79> bytes{};
    // Bounded work even when the consumer has stopped progressing.
    for (unsigned i = 0; i < 16 && endpoint_.available(); ++i) {
        const auto size = endpoint_.read(bytes.data(), options_.fragmentSize);
        if (queuedBytes_ + size > Capacity) {
            if (callbacks_.error)
                callbacks_.error("native bridge USB delivery overflow", now);
            return;
        }
        deliveries_.push_back({now + options_.usbDelayMs, {bytes.begin(), bytes.begin() + size}});
        queuedBytes_ += size;
    }
    while (!deliveries_.empty() && deliveries_.front().due <= now) {
        auto delivery = std::move(deliveries_.front());
        deliveries_.pop_front();
        queuedBytes_ -= delivery.bytes.size();
        if (callbacks_.raw)
            callbacks_.raw("RX", delivery.bytes, now);
        if (generation != generation_ || !open_)
            return;
        if (callbacks_.received)
            callbacks_.received(delivery.bytes, now);
        if (generation != generation_ || !open_)
            return;
    }
}
} // namespace hd::bridge
