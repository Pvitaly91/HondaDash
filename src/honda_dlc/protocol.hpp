#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace hd::dlc {
inline constexpr std::array<std::uint8_t, 11> Initialization{0x68, 0x6a, 0xf5, 0xaf, 0xbf, 0xb3,
                                                             0xb2, 0xc1, 0xdb, 0xb3, 0xe9};
enum class Operation { Read, Write, Reset };
struct Read {
    std::uint16_t address{};
    std::uint16_t length{};
};
inline constexpr std::array<Read, 3> AllowedReads{{{0x00, 2}, {0x14, 1}, {0x10, 1}}};
bool allowed(Read read);
std::uint8_t additiveChecksum(std::span<const std::uint8_t> bytes);
std::optional<std::array<std::uint8_t, 5>> encode(Operation operation, Read read);
std::string hex(std::span<const std::uint8_t> bytes);

// This profile admits only one- and two-byte payloads, hence at most five bytes.
// A parser error is a transaction failure, not evidence of a safe wire resync.
class Parser {
  public:
    enum class Result { Incomplete, Complete, HeaderError, LengthError, ChecksumError };
    void expect(std::size_t payloadLength);
    void reset();
    Result feed(std::uint8_t byte);
    std::span<const std::uint8_t> payload() const;
    std::size_t buffered() const { return size_; }
    static constexpr std::size_t Capacity = 5;

  private:
    std::array<std::uint8_t, Capacity> bytes_{};
    std::size_t size_{}, expected_{5};
};
} // namespace hd::dlc
