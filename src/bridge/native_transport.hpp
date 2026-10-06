#pragma once
#include "../../firmware/nano_dlc_bridge_lab/endpoint.hpp"
#include "application/transport.hpp"
#include <deque>

namespace hd::bridge {
struct NativeOptions {
    std::uint32_t deviceClockOffset{0x70000000};
    Time usbDelayMs{};
    std::size_t fragmentSize{7};
};
struct NativeQueueMetrics {
    std::size_t usbDeliveryBytes{}, outerRxBytes{}, outerTxBytes{}, dlcRxBytes{};
};
// This owns the exact C++11 endpoint compiled for AVR, not a desktop mock.
class NativeTransport final : public Transport {
  public:
    explicit NativeTransport(NativeOptions options = {});
    NativeTransport(const NativeTransport &) = delete;
    NativeTransport &operator=(const NativeTransport &) = delete;
    void start(TransportCallbacks, Time) override;
    void close() override;
    bool send(std::span<const std::uint8_t>, Time) override;
    void tick(Time) override;
    bool isOpen() const override { return open_; }
    std::size_t pendingBytes() const override { return queuedBytes_; }
    std::string name() const override { return "native-bridge-lab"; }
    hd_bridge::BridgeEndpoint &endpoint() { return endpoint_; }
    void resetDevice(Time now);
    void setUsbDelay(Time value) { options_.usbDelayMs = value; }
    const NativeQueueMetrics &queueMetrics() const { return queueMetrics_; }
    static constexpr std::size_t Capacity = 512;

  private:
    struct Delivery {
        Time due;
        std::vector<std::uint8_t> bytes;
    };
    std::uint32_t deviceTime(Time now) const;
    hd_bridge::BridgeEndpoint endpoint_;
    NativeOptions options_;
    TransportCallbacks callbacks_;
    std::deque<Delivery> deliveries_;
    std::size_t queuedBytes_{};
    NativeQueueMetrics queueMetrics_;
    std::uint64_t generation_{};
    bool open_{};
};
} // namespace hd::bridge
