#pragma once
#include "core/model.hpp"
#include <functional>
#include <span>
#include <string>
#include <cstdint>
#include <cstddef>

namespace hd {
struct TransportCallbacks {
    std::function<void(Time)> opened;
    std::function<void(std::span<const std::uint8_t>,Time)> received;
    // All bytes for the one frame have left the transport's local/Qt TX queues.
    // This is not an acknowledgement from the endpoint.
    std::function<void(Time)> sent;
    std::function<void(const std::string&,Time)> error;
    // TX is bytes accepted by the I/O API; RX is exact fragments before parsing.
    std::function<void(const std::string&,std::span<const std::uint8_t>,Time)> raw;
};
class Transport {
public:
    virtual ~Transport() = default;
    virtual void start(TransportCallbacks callbacks,Time now) = 0;
    virtual void close() = 0;
    virtual bool send(std::span<const std::uint8_t> bytes,Time now) = 0;
    virtual void tick(Time now) = 0;
    virtual bool isOpen() const = 0;
    virtual std::size_t pendingBytes() const = 0;
    virtual std::string name() const = 0;
};
}
