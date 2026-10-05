#pragma once
#include "application/transport.hpp"
#include "emulator/emulator.hpp"
#include <deque>
namespace hd {
class InMemoryTransport final : public Transport {
  public:
    void start(TransportCallbacks, Time) override;
    void close() override;
    bool send(std::span<const std::uint8_t>, Time) override;
    void tick(Time) override;
    bool isOpen() const override { return open_; }
    std::size_t pendingBytes() const override { return 0; }
    std::string name() const override { return "in-memory"; }
    Emulator &emulator() { return emulator_; }
    std::size_t pendingDeliveries() const { return deliveries_.size(); }

  private:
    struct Delivery {
        Time due;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<EmulatedReply> dispatch(const Frame &, Time);
    Emulator emulator_;
    Parser parser_;
    TransportCallbacks callbacks_;
    std::deque<Delivery> deliveries_;
    bool open_{};
    std::uint32_t session_{}, lastRequest_{};
    std::uint64_t generation_{};
    std::vector<std::uint8_t> lastBytes_;
    std::vector<EmulatedReply> lastReply_;
};
} // namespace hd
