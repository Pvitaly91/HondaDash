#include "protocol/protocol.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hd {
namespace {
constexpr std::size_t HeaderSize = 13;
constexpr std::size_t FrameOverhead = 15;
constexpr std::array<double, ChannelCount> scales{1, 10, 10, 10, 10, 10, 100};
constexpr std::int16_t Absent = std::numeric_limits<std::int16_t>::min();

void append32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::uint32_t read32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
    return value;
}
std::uint16_t read16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[at]) |
        (static_cast<unsigned>(bytes[at + 1]) << 8));
}
bool validCandidate(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (bytes.size() - at < HeaderSize || bytes[at] != 0xa5 || bytes[at + 1] != 0x5a ||
        bytes[at + 2] != ProtocolVersion || bytes[at + 12] > MaxPayload)
        return false;
    const auto length = FrameOverhead + static_cast<std::size_t>(bytes[at + 12]);
    return bytes.size() - at >= length &&
        crc16(bytes.subspan(at + 2, length - 4)) == read16(bytes, at + length - 2);
}
}

std::uint16_t crc16(std::span<const std::uint8_t> bytes) {
    std::uint16_t crc = 0xffff;
    for (const auto byte : bytes) {
        crc ^= static_cast<std::uint16_t>(static_cast<unsigned>(byte) << 8);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

std::vector<std::uint8_t> encode(const Frame& frame) {
    if (frame.payload.size() > MaxPayload)
        throw std::invalid_argument("synthetic frame payload exceeds 64 bytes");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(FrameOverhead + frame.payload.size());
    bytes.insert(bytes.end(), {0xa5, 0x5a, ProtocolVersion, frame.type});
    append32(bytes, frame.session);
    append32(bytes, frame.request);
    bytes.push_back(static_cast<std::uint8_t>(frame.payload.size()));
    bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
    const auto crc = crc16(std::span<const std::uint8_t>(bytes).subspan(2));
    bytes.push_back(static_cast<std::uint8_t>(crc));
    bytes.push_back(static_cast<std::uint8_t>(crc >> 8));
    return bytes;
}

std::vector<Frame> Parser::feed(std::span<const std::uint8_t> bytes) {
    std::vector<Frame> frames;
    for (const auto byte : bytes) {
        buffer_.push_back(byte);
        drain(frames);
    }
    return frames;
}

void Parser::drain(std::vector<Frame>& frames) {
    while (!buffer_.empty()) {
        // Retain a single A5 at the end so SOF may straddle fragments.
        if (buffer_[0] != 0xa5 || (buffer_.size() >= 2 && buffer_[1] != 0x5a)) {
            buffer_.erase(buffer_.begin());
            continue;
        }
        if (buffer_.size() < 3) return;
        if (buffer_[2] != ProtocolVersion) {
            ++errors_;
            buffer_.erase(buffer_.begin());
            continue;
        }
        if (buffer_.size() < HeaderSize) return;
        if (buffer_[12] > MaxPayload) {
            ++errors_;
            buffer_.erase(buffer_.begin());
            continue;
        }
        const auto length = FrameOverhead + static_cast<std::size_t>(buffer_[12]);
        if (buffer_.size() < length) {
            // A lost tail must not indefinitely hold a later complete, checked frame.
            // Inner SOF bytes alone cannot preempt a valid outer frame.
            bool recovered = false;
            for (std::size_t at = 2; at + FrameOverhead <= buffer_.size(); ++at) {
                if (validCandidate(buffer_, at)) {
                    ++errors_;
                    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(at));
                    recovered = true;
                    break;
                }
            }
            if (recovered) continue;
            return;
        }
        if (!validCandidate(buffer_, 0)) {
            ++errors_;
            buffer_.erase(buffer_.begin());
            continue;
        }
        Frame frame;
        frame.type = buffer_[3];
        frame.session = read32(buffer_, 4);
        frame.request = read32(buffer_, 8);
        frame.payload.assign(buffer_.begin() + HeaderSize,
            buffer_.begin() + HeaderSize + buffer_[12]);
        frames.push_back(std::move(frame));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(length));
    }
}

void Parser::reset() { buffer_.clear(); } // Error counter is cumulative for this parser instance.

std::vector<std::uint8_t> encodeSnapshot(const Sample& sample) {
    std::vector<std::uint8_t> payload;
    payload.reserve(ChannelCount * 3);
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        auto quality = sample.qualities[i];
        std::int16_t raw = Absent;
        const auto& info = channelInfo(static_cast<Channel>(i));
        if (quality == Quality::Valid && sample.values[i] && std::isfinite(*sample.values[i]) &&
            *sample.values[i] >= info.min && *sample.values[i] <= info.max) {
            raw = static_cast<std::int16_t>(std::lround(*sample.values[i] * scales[i]));
        } else if (quality != Quality::Unsupported) {
            quality = Quality::Invalid;
        }
        payload.push_back(quality == Quality::Unsupported ? 3 : raw == Absent ? 4 : 1);
        const auto unsignedRaw = static_cast<std::uint16_t>(raw);
        payload.push_back(static_cast<std::uint8_t>(unsignedRaw));
        payload.push_back(static_cast<std::uint8_t>(unsignedRaw >> 8));
    }
    return payload;
}

std::optional<Sample> decodeSnapshot(const Frame& frame, Time receivedAt) {
    if (frame.type != SnapshotResponse || frame.payload.size() != ChannelCount * 3)
        return std::nullopt;
    Sample sample;
    sample.time = receivedAt;
    sample.session = frame.session;
    sample.request = frame.request;
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        const auto at = i * 3;
        const auto status = frame.payload[at];
        const auto bits = read16(frame.payload, at + 1);
        const auto raw = static_cast<std::int32_t>(bits) - (bits >= 0x8000 ? 65536 : 0);
        if (status == 3 || status == 4) {
            if (raw != Absent) return std::nullopt;
            sample.qualities[i] = status == 3 ? Quality::Unsupported : Quality::Invalid;
        } else if (status == 1 && raw != Absent) {
            const auto value = static_cast<double>(raw) / scales[i];
            const auto& info = channelInfo(static_cast<Channel>(i));
            if (value < info.min || value > info.max) {
                sample.qualities[i] = Quality::Invalid;
            } else {
                sample.values[i] = value;
                sample.qualities[i] = Quality::Valid;
            }
        } else {
            return std::nullopt;
        }
    }
    return sample;
}

} // namespace hd
