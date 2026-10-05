#pragma once

#include "core/model.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hd {

inline constexpr std::uint8_t ProtocolVersion = 1;
inline constexpr std::uint8_t Hello = 0x01;
inline constexpr std::uint8_t ReadSnapshot = 0x02;
inline constexpr std::uint8_t HelloResponse = 0x81;
inline constexpr std::uint8_t SnapshotResponse = 0x82;
inline constexpr std::uint8_t ErrorResponse = 0xff;
inline constexpr std::size_t MaxPayload = 64;
inline constexpr std::size_t MaxFrame = 79;

struct Frame {
    std::uint8_t type{};
    std::uint32_t session{};
    std::uint32_t request{};
    std::vector<std::uint8_t> payload;
};

std::uint16_t crc16(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode(const Frame& frame);

class Parser {
public:
    std::vector<Frame> feed(std::span<const std::uint8_t> bytes);
    void reset();
    std::uint64_t errors() const { return errors_; }
    std::size_t buffered() const { return buffer_.size(); }

private:
    void drain(std::vector<Frame>& frames);
    std::vector<std::uint8_t> buffer_;
    std::uint64_t errors_{};
};

std::optional<Sample> decodeSnapshot(const Frame& frame, Time receivedAt);
std::vector<std::uint8_t> encodeSnapshot(const Sample& sample);

} // namespace hd
