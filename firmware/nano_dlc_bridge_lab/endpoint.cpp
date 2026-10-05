#include "endpoint.hpp"
#include <string.h>
namespace hd_bridge {
namespace {
void increment(uint32_t &value) {
    if (value != UINT32_MAX)
        ++value;
}
uint16_t crc16(const uint8_t *bytes, uint8_t length) {
    uint16_t crc = 0xffff;
    for (uint8_t i = 0; i < length; ++i) {
        crc ^= uint16_t(bytes[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}
uint16_t elapsed16(uint32_t value) {
    return uint16_t(value > 65535u ? 65535u : value);
}
} // namespace
BridgeEndpoint::BridgeEndpoint() : engine_(ecu_) {
    reset(0);
}
void BridgeEndpoint::reset(uint32_t now) {
    ecu_ = VirtualHondaEcu();
    engine_.newExperiment();
    engine_.abort(now);
    rxSize_ = txHead_ = txSize_ = lastRequestSize_ = lastReplySize_ = 0;
    session_ = lastId_ = activeId_ = generation_ = 0;
    experimentAt_ = now;
    eventSequence_ = 0;
    bound_ = false;
    memset(&counters_, 0, sizeof counters_);
}
void BridgeEndpoint::discard(uint8_t count) {
    rxSize_ = uint8_t(rxSize_ - count);
    memmove(rx_, rx_ + count, rxSize_);
}
void BridgeEndpoint::receive(uint8_t byte, uint32_t now) {
    if (rxSize_ == MaxFrame) {
        discard(1);
        increment(counters_.parserErrors);
    }
    rx_[rxSize_++] = byte;
    for (uint8_t work = 0; work < MaxFrame && rxSize_; ++work) {
        if (rx_[0] != 0xa5) {
            discard(1);
            increment(counters_.parserErrors);
            continue;
        }
        if (rxSize_ < 2)
            break;
        if (rx_[1] != 0x5a) {
            discard(1);
            increment(counters_.parserErrors);
            continue;
        }
        if (rxSize_ < 13)
            break;
        if (rx_[2] != 1 || rx_[12] > MaxPayload) {
            discard(1);
            increment(counters_.parserErrors);
            continue;
        }
        const uint8_t size = uint8_t(rx_[12] + 15);
        if (rxSize_ < size)
            break;
        if (load16(rx_ + size - 2) != crc16(rx_ + 2, uint8_t(size - 4))) {
            discard(1);
            increment(counters_.parserErrors);
            continue;
        }
        increment(counters_.frames);
        dispatch(size, now);
        discard(size);
    }
}
bool BridgeEndpoint::enqueue(const uint8_t *bytes, uint8_t size) {
    if (uint16_t(txSize_) + size > TxCapacity)
        return false;
    for (uint8_t i = 0; i < size; ++i) {
        const uint16_t at = uint16_t(txHead_) + txSize_;
        tx_[at % TxCapacity] = bytes[i];
        ++txSize_;
    }
    return true;
}
void BridgeEndpoint::reply(uint8_t type, uint32_t session, uint32_t request, const uint8_t *payload,
                           uint8_t size, bool cache) {
    if (size > MaxPayload) {
        increment(counters_.lostEvents);
        increment(counters_.txOverflows);
        return;
    }
    uint8_t frame[MaxFrame];
    frame[0] = 0xa5;
    frame[1] = 0x5a;
    frame[2] = 1;
    frame[3] = type;
    store32(frame + 4, session);
    store32(frame + 8, request);
    frame[12] = size;
    memcpy(frame + 13, payload, size);
    store16(frame + 13 + size, crc16(frame + 2, uint8_t(size + 11)));
    const uint8_t count = uint8_t(size + 15);
    if (!enqueue(frame, count)) {
        increment(counters_.lostEvents);
        return;
    }
    if (cache) {
        memcpy(lastReply_, frame, count);
        lastReplySize_ = count;
    }
}
void BridgeEndpoint::ack(uint8_t type, uint8_t command, uint8_t status, uint32_t session, uint32_t request,
                         bool cache) {
    uint8_t payload[AckSize] = {Version, 0, 0, 0, 0, command, status};
    store32(payload + 1, generation_);
    reply(type, session, request, payload, sizeof payload, cache);
}
void BridgeEndpoint::failOverflow(uint32_t now) {
    increment(counters_.txOverflows);
    increment(counters_.lostEvents);
    txHead_ = txSize_ = lastReplySize_ = 0;
    engine_.abort(now, Overflow);
    DlcResult discarded;
    engine_.takeResult(discarded);
    activeId_ = 0;
    ack(Error, 0, Overflow, session_, lastId_);
}
void BridgeEndpoint::dispatch(uint8_t frameSize, uint32_t now) {
    const uint8_t command = rx_[3], size = rx_[12];
    const uint32_t session = load32(rx_ + 4), request = load32(rx_ + 8);
    const uint8_t *payload = rx_ + 13;
    const bool terminates = command == Hello || command == NewExperiment || command == Abort;
    const uint16_t reserve = activeId_ && terminates ? TxCapacity : MaxFrame;
    if (uint16_t(txSize_) + reserve > TxCapacity) {
        failOverflow(now);
        return;
    }
    if (!session || !request) {
        ack(Error, command, BadPayload, session, request);
        return;
    }
    if (command < Hello || command > Diagnostics) {
        ack(Error, command, BadCommand, session, request);
        return;
    }
    if (bound_ && session == session_ && request == lastId_) {
        if (frameSize == lastRequestSize_ && memcmp(rx_, lastRequest_, frameSize) == 0) {
            increment(counters_.duplicates);
            if (lastReplySize_ && !enqueue(lastReply_, lastReplySize_))
                failOverflow(now);
        } else
            ack(Error, command, IdConflict, session, request);
        return;
    }
    if (command != Hello && (!bound_ || session != session_)) {
        ack(Error, command, NotBound, session, request);
        return;
    }
    if (bound_ && session == session_ && request < lastId_) {
        ack(Error, command, StaleRequest, session, request);
        return;
    }
    if (command == Hello && (size != 2 || payload[0] != Version || payload[1] != PolicyVersion)) {
        ack(Error, command, PolicyDenied, session, request);
        return;
    }
    if (command == Hello) {
        // A new binding invalidates any old operation, but preserves late DLC bytes.
        engine_.abort(now);
        publishResult(); // Retain actual TX/partial RX under the old outer session.
        activeId_ = 0;
        session_ = session;
        bound_ = true;
    }
    lastId_ = request;
    memcpy(lastRequest_, rx_, frameSize);
    lastRequestSize_ = frameSize;
    lastReplySize_ = 0;
    if (command == Hello) {
        uint8_t info[MaxPayload] = {Version, PolicyVersion, BackendVirtual, 0, Capabilities, 0};
        store32(info + 6, generation_);
        info[10] = engine_.state();
        info[11] = 1;
        info[12] = 0;
        info[13] = 0;
        info[14] = sizeof Identity - 1;
        memcpy(info + 15, Identity, sizeof Identity - 1);
        reply(HelloInfo, session, request, info, uint8_t(15 + sizeof Identity - 1), true);
        return;
    }
    if (!size || payload[0] != PolicyVersion) {
        ack(Error, command, PolicyDenied, session, request, true);
        return;
    }
    if ((command == Configure && size != 7) || (command == Execute && size != 7) ||
        (command != Configure && command != Execute && size != 1)) {
        ack(Error, command, BadPayload, session, request, true);
        return;
    }
    if (command == NewExperiment) {
        engine_.abort(now);
        publishResult();
        ecu_.newExperiment();
        engine_.newExperiment();
        ++generation_;
        if (!generation_)
            ++generation_;
        eventSequence_ = 0;
        experimentAt_ = now;
        activeId_ = 0;
        ack(Ack, command, Ok, session, request, true);
        return;
    }
    if (command == Abort) {
        engine_.abort(now);
        publishResult();
        ack(Ack, command, Ok, session, request, true);
        return;
    }
    if (command == Diagnostics) {
        uint8_t info[26] = {Version};
        store32(info + 1, generation_);
        info[5] = engine_.state();
        info[6] = engine_.activeCommand();
        info[7] = ecu_.pending();
        store16(info + 8, txSize_);
        store32(info + 10, ecu_.txBytes());
        store32(info + 14, counters_.parserErrors);
        store32(info + 18, counters_.txOverflows);
        store32(info + 22, counters_.lostEvents);
        reply(DiagnosticInfo, session, request, info, sizeof info, true);
        return;
    }
    if (engine_.activeCommand()) {
        ack(Error, command, Busy, session, request, true);
        return;
    }
    if (command == Configure) {
        const bool accepted =
            ecu_.configure(payload[1], payload[2], load16(payload + 3), load16(payload + 5));
        ack(accepted ? Ack : Error, command, accepted ? Ok : BadPayload, session, request, true);
        return;
    }
    if (engine_.state() == Faulted || engine_.state() == NeedsExperiment) {
        ack(Error, command, NeedsNewExperiment, session, request, true);
        return;
    }
    if (command == Execute && !TransactionEngine::allowed(payload + 2, 5, payload[1])) {
        ack(Error, command, PolicyDenied, session, request, true);
        return;
    }
    const bool accepted =
        command == Initialize ? engine_.initialize(now) : engine_.execute(payload + 2, 5, payload[1], now);
    if (!accepted) {
        ack(Error, command, NotInitialized, session, request, true);
        return;
    }
    activeId_ = request;
}
void BridgeEndpoint::publishResult() {
    DlcResult result;
    if (!engine_.takeResult(result))
        return;
    uint8_t payload[MaxPayload] = {Version};
    store32(payload + 1, generation_);
    payload[5] = result.status;
    payload[6] = result.command;
    payload[7] = result.address;
    payload[8] = result.readLength;
    payload[9] = result.txLength;
    payload[10] = result.rxLength;
    store16(payload + 11, result.txElapsed);
    store16(payload + 13, result.responseElapsed);
    store16(payload + 15, result.maxGap);
    memcpy(payload + ResultHeader, result.tx, result.txLength);
    memcpy(payload + ResultHeader + result.txLength, result.rx, result.rxLength);
    reply(Result, session_, activeId_, payload, uint8_t(ResultHeader + result.txLength + result.rxLength),
          activeId_ == lastId_);
    activeId_ = 0;
}
void BridgeEndpoint::tick(uint32_t now) {
    engine_.tick(now);
    if (uint16_t(txSize_) + MaxFrame > TxCapacity) {
        if (engine_.activeCommand())
            failOverflow(now);
        // Keep draining physical-side bytes even while the USB reader is stalled.
        uint8_t discarded[MaxDlcRx];
        if (engine_.takeLate(discarded, sizeof discarded))
            failOverflow(now);
        return;
    }
    publishResult();
    uint8_t payload[EventHeader + MaxDlcRx] = {Version};
    const uint8_t count = engine_.takeLate(payload + EventHeader, MaxDlcRx);
    if (!count)
        return;
    if (uint16_t(txSize_) + MaxFrame > TxCapacity) {
        failOverflow(now);
        return;
    }
    store32(payload + 1, generation_);
    store16(payload + 5, ++eventSequence_);
    payload[7] = DlcUnexpected;
    store16(payload + 8, elapsed16(uint32_t(now - experimentAt_)));
    payload[10] = count;
    reply(Event, session_, 0, payload, uint8_t(EventHeader + count));
}
size_t BridgeEndpoint::read(uint8_t *output, size_t capacity) {
    const size_t count = capacity < txSize_ ? capacity : txSize_;
    for (size_t i = 0; i < count; ++i) {
        output[i] = tx_[txHead_];
        txHead_ = uint8_t((txHead_ + 1) % TxCapacity);
    }
    txSize_ = uint8_t(txSize_ - count);
    return count;
}
} // namespace hd_bridge
