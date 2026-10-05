#include "honda_dlc/protocol.hpp"
#include <algorithm>
#include <stdexcept>

namespace hd::dlc {
bool allowed(Read read) {
    // Check the wider inputs before any narrowing or address arithmetic.
    if (read.address > 255 || read.length == 0 || read.length > 2 || read.address + read.length > 256)
        return false;
    return std::any_of(AllowedReads.begin(), AllowedReads.end(), [read](Read candidate) {
        return read.address == candidate.address && read.length == candidate.length;
    });
}
std::uint8_t additiveChecksum(std::span<const std::uint8_t> bytes) {
    unsigned sum = 0;
    for (auto byte : bytes)
        sum = (sum + byte) & 0xffu;
    return static_cast<std::uint8_t>((256u - sum) & 0xffu);
}
std::optional<std::array<std::uint8_t, 5>> encode(Operation operation, Read read) {
    if (operation != Operation::Read || !allowed(read))
        return std::nullopt;
    std::array<std::uint8_t, 5> result{0x20, 0x05, static_cast<std::uint8_t>(read.address),
                                       static_cast<std::uint8_t>(read.length), 0};
    result.back() = additiveChecksum(std::span(result).first(4));
    return result;
}
std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (auto byte : bytes) {
        if (!result.empty())
            result += ' ';
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
void Parser::expect(std::size_t payloadLength) {
    if (payloadLength < 1 || payloadLength > 2)
        throw std::invalid_argument("DLC payload outside profile");
    expected_ = payloadLength + 3;
    reset();
}
void Parser::reset() {
    size_ = 0;
}
Parser::Result Parser::feed(std::uint8_t byte) {
    if (size_ == expected_)
        reset();
    if (size_ == 0 && byte != 0)
        return Result::HeaderError;
    if (size_ == 1 && byte != expected_) {
        reset();
        return Result::LengthError;
    }
    bytes_[size_++] = byte;
    if (size_ < expected_)
        return Result::Incomplete;
    if (additiveChecksum(std::span(bytes_).first(size_)) != 0) {
        reset();
        return Result::ChecksumError;
    }
    return Result::Complete;
}
std::span<const std::uint8_t> Parser::payload() const {
    if (size_ != expected_)
        return {};
    return std::span(bytes_).subspan(2, expected_ - 3);
}
} // namespace hd::dlc
