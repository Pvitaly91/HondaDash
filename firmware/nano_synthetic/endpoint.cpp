#include "endpoint.hpp"

#include <string.h>

namespace hd_nano {
namespace {
uint16_t load16(const uint8_t* bytes) {
    return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) |
                                (static_cast<uint16_t>(bytes[1]) << 8));
}
int16_t signed16(const uint8_t* bytes) {
    const uint16_t bits = load16(bytes);
    // Convert via int32_t: conversion of out-of-range uint16_t to int16_t is
    // implementation-defined. Both 16-bit AVR int and 32-bit host int are safe.
    return static_cast<int16_t>(static_cast<int32_t>(bits) -
                               (bits >= 0x8000U ? INT32_C(65536) : INT32_C(0)));
}
uint32_t load32(const uint8_t* bytes) {
    uint32_t result = 0;
    for (uint8_t i = 0; i < 4; ++i)
        result |= static_cast<uint32_t>(bytes[i]) << (static_cast<uint8_t>(8 * i));
    return result;
}
void save16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}
void save32(uint8_t* bytes, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i)
        bytes[i] = static_cast<uint8_t>(value >> static_cast<uint8_t>(8 * i));
}
uint16_t crc(const uint8_t* bytes, uint8_t length) {
    uint16_t result = 0xffff;
    for (uint8_t i = 0; i < length; ++i) {
        result ^= static_cast<uint16_t>(static_cast<uint16_t>(bytes[i]) << 8);
        for (uint8_t bit = 0; bit < 8; ++bit)
            result = static_cast<uint16_t>((result & 0x8000U) ?
                static_cast<uint16_t>(result << 1) ^ 0x1021U : result << 1);
    }
    return result;
}
void increment(uint32_t& value) { if (value != UINT32_MAX) ++value; }
int16_t jitter(uint32_t seed, uint32_t tick) {
    uint32_t value = seed ^ tick;
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return static_cast<int16_t>(value % UINT32_C(29)) - 14;
}
}

Endpoint::Endpoint() { reset(0); }

void Endpoint::reset(uint32_t now) {
    rxSize_ = txHead_ = txSize_ = lastRequestSize_ = lastResponseSize_ = delayedSize_ = 0;
    delayedAt_ = now;
    delayedWait_ = 0;
    initialized_ = false;
    session_ = lastId_ = 0;
    scenario_ = 2;
    epoch_ = now;
    seed_ = 1;
    const int16_t defaults[ChannelCount] = {850, 0, 850, 300, 20, 320, 1420};
    for (uint8_t i = 0; i < ChannelCount; ++i) { manual_[i] = defaults[i]; quality_[i] = 1; }
    silent_ = false;
    delayMs_ = 0;
    once_ = 0;
    counters_.frames = counters_.parserErrors = counters_.txOverflows = counters_.duplicates = 0;
}

bool Endpoint::candidate(uint8_t at) const {
    const uint8_t left = static_cast<uint8_t>(rxSize_ - at);
    if (left < 15 || rx_[at] != 0xa5 || rx_[at + 1] != 0x5a ||
        rx_[at + 2] != 1 || rx_[at + 12] > MaxPayload) return false;
    const uint8_t length = static_cast<uint8_t>(15 + rx_[at + 12]);
    return left >= length && crc(rx_ + at + 2, static_cast<uint8_t>(length - 4)) ==
        load16(rx_ + at + length - 2);
}

void Endpoint::discard(uint8_t count) {
    rxSize_ = static_cast<uint8_t>(rxSize_ - count);
    memmove(rx_, rx_ + count, rxSize_);
}

void Endpoint::receive(uint8_t byte, uint32_t now) {
    if (rxSize_ == MaxFrame) { increment(counters_.parserErrors); discard(1); }
    rx_[rxSize_++] = byte;
    drain(now);
}

void Endpoint::drain(uint32_t now) {
    while (rxSize_ != 0) {
        if (rx_[0] != 0xa5 || (rxSize_ >= 2 && rx_[1] != 0x5a)) { discard(1); continue; }
        if (rxSize_ < 3) return;
        if (rx_[2] != 1) { increment(counters_.parserErrors); discard(1); continue; }
        if (rxSize_ < 13) return;
        if (rx_[12] > MaxPayload) { increment(counters_.parserErrors); discard(1); continue; }
        const uint8_t length = static_cast<uint8_t>(15 + rx_[12]);
        if (rxSize_ < length) {
            uint8_t recovered = 0;
            for (uint8_t at = 2; static_cast<uint16_t>(at) + 15 <= rxSize_; ++at) {
                if (candidate(at)) { recovered = at; break; }
            }
            if (!recovered) return;
            increment(counters_.parserErrors);
            discard(recovered);
            continue;
        }
        if (!candidate(0)) { increment(counters_.parserErrors); discard(1); continue; }
        increment(counters_.frames);
        dispatch(length, now);
        discard(length);
    }
}

uint8_t Endpoint::response(uint8_t* output, uint8_t type, uint32_t session,
                           uint32_t request, const uint8_t* payload, uint8_t length) const {
    output[0] = 0xa5; output[1] = 0x5a; output[2] = 1; output[3] = type;
    save32(output + 4, session); save32(output + 8, request); output[12] = length;
    if (length != 0) memcpy(output + 13, payload, length);
    save16(output + 13 + length, crc(output + 2, static_cast<uint8_t>(11 + length)));
    return static_cast<uint8_t>(15 + length);
}

void Endpoint::enqueue(const uint8_t* bytes, uint8_t length) {
    for (uint8_t i = 0; i < length; ++i) {
        const uint16_t position = static_cast<uint16_t>(txHead_) + txSize_;
        tx_[position % TxCapacity] = bytes[i];
        ++txSize_;
    }
}

void Endpoint::error(uint8_t code, uint32_t session, uint32_t request) {
    uint8_t bytes[16];
    const uint8_t length = response(bytes, 0xff, session, request, &code, 1);
    enqueue(bytes, length);
}

void Endpoint::dispatch(uint8_t length, uint32_t now) {
    const uint8_t type = rx_[3];
    const uint32_t session = load32(rx_ + 4);
    const uint32_t request = load32(rx_ + 8);
    const uint8_t size = rx_[12];
    const uint8_t* payload = rx_ + 13;

    // A malformed sender can outrun UART output. Fail visibly and require HELLO;
    // no command side effect is accepted without reserved response capacity.
    if (static_cast<uint16_t>(txSize_) + MaxFrame > TxCapacity) {
        increment(counters_.txOverflows);
        txHead_ = txSize_ = delayedSize_ = lastRequestSize_ = lastResponseSize_ = 0;
        initialized_ = false;
        error(4, session, request);
        return;
    }
    // A malformed HELLO from another identity must not advance this session's
    // duplicate cache or invalidate its ordering. Only an accepted HELLO binds it.
    if (type == 1 && size != 0) { error(2, session, request); return; }
    if (initialized_ && session == session_ && request == lastId_) {
        if (length == lastRequestSize_ && memcmp(rx_, lastRequest_, length) == 0) {
            increment(counters_.duplicates);
            // A pending delayed duplicate neither resets its deadline nor emits early.
            if (!delayedSize_ || type != 2) enqueue(lastResponse_, lastResponseSize_);
        } else error(5, session, request);
        return;
    }
    if (type != 1 && (!initialized_ || session != session_)) { error(3, session, request); return; }
    if (initialized_ && session == session_ && request < lastId_) { error(5, session, request); return; }

    uint8_t out[21];
    uint8_t outSize = 1;
    uint8_t outType = static_cast<uint8_t>(type | 0x80U);
    uint8_t failure = 0;
    out[0] = 0;
    switch (type) {
    case 1: {
        if (size != 0) { failure = 2; break; }
        // New identity cancels old delayed and queued bytes; hardware UART bytes
        // already accepted by Serial are rejected by the PC's session matching.
        if (!initialized_ || session != session_) {
            txHead_ = txSize_ = delayedSize_ = 0;
            lastRequestSize_ = lastResponseSize_ = 0;
        }
        initialized_ = true;
        session_ = session;
        const char identity[] = "synthetic-demo-v1";
        out[0] = 1;
        memcpy(out + 1, identity, sizeof(identity) - 1);
        outSize = static_cast<uint8_t>(sizeof(identity));
        break;
    }
    case 2:
        if (size != 0) failure = 2;
        else if (delayedSize_) failure = 4;
        else { snapshot(out, now); outSize = 21; }
        break;
    case 0x10: {
        if (size != 0) { failure = 2; break; }
        const char name[] = "nano-synthetic";
        out[0] = 1; out[1] = 2; out[2] = 0; out[3] = 2; out[4] = 0;
        out[5] = 0x0f; out[6] = 0;
        memcpy(out + 7, name, sizeof(name) - 1);
        outSize = static_cast<uint8_t>(7 + sizeof(name) - 1);
        break;
    }
    case 0x11:
        if (size != 5 || payload[0] > 3) failure = 2;
        else { scenario_ = payload[0]; seed_ = load32(payload + 1); epoch_ = now; }
        break;
    case 0x12: {
        if (size != 14) { failure = 2; break; }
        const int16_t minimum[ChannelCount] = {0, 0, -400, -400, 0, 0, 0};
        const int16_t maximum[ChannelCount] = {12000, 2500, 1500, 1200, 1000, 2500, 2000};
        for (uint8_t i = 0; i < ChannelCount; ++i) {
            const int16_t value = signed16(payload + 2 * i);
            if (value < minimum[i] || value > maximum[i]) failure = 2;
        }
        if (!failure) for (uint8_t i = 0; i < ChannelCount; ++i) manual_[i] = signed16(payload + 2 * i);
        break;
    }
    case 0x13:
        if (size != 4 || payload[0] > 1 || load16(payload + 1) > 5000 || payload[3] > 3) failure = 2;
        else { silent_ = payload[0] != 0; delayMs_ = load16(payload + 1); once_ = payload[3]; }
        break;
    case 0x14:
        if (size != 7) { failure = 2; break; }
        for (uint8_t i = 0; i < ChannelCount; ++i)
            if (payload[i] != 1 && payload[i] != 3 && payload[i] != 4) failure = 2;
        if (!failure) memcpy(quality_, payload, ChannelCount);
        break;
    default: failure = 1; break;
    }
    if (failure) { outType = 0xff; out[0] = failure; outSize = 1; }
    memcpy(lastRequest_, rx_, length);
    lastRequestSize_ = length;
    lastId_ = request;
    lastResponseSize_ = response(lastResponse_, outType, session, request, out, outSize);
    if (type == 2 && !failure) {
        if (silent_) { lastResponseSize_ = 0; return; }
        if ((once_ & 1) != 0) lastResponse_[lastResponseSize_ - 1] ^= 0x80;
        if ((once_ & 2) != 0) lastResponseSize_ = static_cast<uint8_t>(lastResponseSize_ / 2);
        once_ = 0;
        if (delayMs_ != 0) {
            memcpy(delayed_, lastResponse_, lastResponseSize_);
            delayedSize_ = lastResponseSize_;
            delayedAt_ = now;
            delayedWait_ = delayMs_;
            return;
        }
    }
    enqueue(lastResponse_, lastResponseSize_);
}

void Endpoint::tick(uint32_t now) {
    // Unsigned subtraction remains correct across millis() wrap; delays <=5000ms.
    if (delayedSize_ && static_cast<uint32_t>(now - delayedAt_) >= delayedWait_ &&
        static_cast<uint16_t>(txSize_) + delayedSize_ <= TxCapacity) {
        enqueue(delayed_, delayedSize_);
        delayedSize_ = 0;
    }
}

size_t Endpoint::read(uint8_t* destination, size_t capacity) {
    size_t count = 0;
    while (count < capacity && txSize_ != 0) {
        destination[count++] = tx_[txHead_];
        txHead_ = static_cast<uint8_t>((static_cast<uint16_t>(txHead_) + 1) % TxCapacity);
        --txSize_;
    }
    return count;
}

void Endpoint::snapshot(uint8_t* payload, uint32_t now) const {
    int16_t values[ChannelCount] = {0, 0, 220, 220, 0, 1000, 1250};
    const uint32_t elapsed = static_cast<uint32_t>(now - epoch_);
    const int16_t variation = jitter(seed_, elapsed / UINT32_C(100));
    if (scenario_ == 3) memcpy(values, manual_, sizeof(values));
    else if (scenario_ == 1) {
        values[0] = static_cast<int16_t>(850 + variation);
        values[2] = 850; values[3] = 300; values[4] = 20; values[5] = 320; values[6] = 1420;
    } else if (scenario_ == 2) {
        // Integer-only 80-second synthetic demonstration. The desktop's cosine
        // acceleration is intentionally approximated by a triangle on Nano.
        const uint32_t phase = elapsed % UINT32_C(80000);
        if (phase >= 3000 && phase < 5000) {
            const uint32_t step = phase - 3000;
            values[0] = static_cast<int16_t>(250 + step * UINT32_C(525) / 1000);
            values[4] = 40; values[5] = 550;
            values[6] = static_cast<int16_t>(1180 + step * UINT32_C(120) / 1000);
        } else if (phase >= 5000 && phase < 25000) {
            const uint32_t step = phase - 5000;
            values[0] = static_cast<int16_t>(1300 - static_cast<int32_t>(step * UINT32_C(450) / 20000) + variation);
            values[2] = static_cast<int16_t>(220 + step * UINT32_C(630) / 20000);
            values[3] = static_cast<int16_t>(220 + step * UINT32_C(80) / 20000);
            values[4] = static_cast<int16_t>(30 - step * UINT32_C(10) / 20000);
            values[5] = static_cast<int16_t>(340 - step * UINT32_C(20) / 20000);
            values[6] = 1420;
        } else if (phase >= 25000 && phase < 60000) {
            const uint32_t ramp = (phase - 25000) % UINT32_C(10000);
            const uint32_t wave = ramp < 5000 ? ramp : 10000 - ramp;
            values[0] = static_cast<int16_t>(INT32_C(850) +
                static_cast<int32_t>(wave * UINT32_C(4600) / 5000) + variation);
            values[1] = static_cast<int16_t>(wave * UINT32_C(1022) / 5000);
            values[2] = static_cast<int16_t>(850 + wave * UINT32_C(40) / 5000);
            values[3] = static_cast<int16_t>(300 + wave * UINT32_C(60) / 5000);
            values[4] = static_cast<int16_t>(50 + wave * UINT32_C(650) / 5000);
            values[5] = static_cast<int16_t>(320 + wave * UINT32_C(630) / 5000);
            values[6] = static_cast<int16_t>(1420 - wave * UINT32_C(15) / 5000);
        } else if (phase >= 60000 && phase < 70000) {
            values[0] = static_cast<int16_t>(850 + variation);
            values[2] = 860; values[3] = 320; values[4] = 20; values[5] = 320; values[6] = 1420;
        } else if (phase >= 70000) {
            const uint32_t step = phase - 70000;
            values[2] = static_cast<int16_t>(860 - step * UINT32_C(3) / 1000);
            values[3] = static_cast<int16_t>(320 - step / 1000);
        }
    }
    for (uint8_t i = 0; i < ChannelCount; ++i) {
        payload[3 * i] = quality_[i];
        save16(payload + 3 * i + 1, quality_[i] == 1 ? static_cast<uint16_t>(values[i]) : 0x8000U);
    }
}
} // namespace hd_nano
