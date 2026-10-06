#include "endpoint.hpp"
#include "../nano_dlc_bridge_lab/transaction_engine.hpp"
#include "../shared/reference_fixtures.hpp"
#include <string.h>
namespace hd_bench {
using namespace hd_bridge;
namespace {
void increment(uint32_t &value) {
    if (value != UINT32_MAX)
        ++value;
}
uint16_t crc16(const uint8_t *bytes, uint8_t length) {
    uint16_t value = 0xffff;
    for (uint8_t i = 0; i < length; ++i) {
        value ^= uint16_t(bytes[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            value = value & 0x8000 ? uint16_t((value << 1) ^ 0x1021) : uint16_t(value << 1);
    }
    return value;
}
} // namespace
ResponderEndpoint::ResponderEndpoint(DlcPort &port) : port_(port) {
    resetBuffers(0);
}
void ResponderEndpoint::reset(uint32_t now) {
    port_.abortTx();
    resetBuffers(now);
}
void ResponderEndpoint::resetBuffers(uint32_t now) {
    rxSize_ = txHead_ = txSize_ = lastRequestSize_ = lastReplySize_ = inputSize_ = replySize_ = replyAt_ = 0;
    session_ = lastId_ = generation_ = quiesceRequest_ = 0;
    inputAt_ = initAt_ = replyAtMs_ = now;
    delay_ = gap_ = activeGap_ = 0;
    scenario_ = fault_ = 0;
    bound_ = armed_ = initialized_ = quiescing_ = responseStarted_ = lineFaulted_ = false;
    memset(&counters_, 0, sizeof counters_);
}
void ResponderEndpoint::discard(uint8_t count) {
    rxSize_ = uint8_t(rxSize_ - count);
    memmove(rx_, rx_ + count, rxSize_);
}
void ResponderEndpoint::receive(uint8_t byte, uint32_t now) {
    if (rxSize_ == MaxFrame) {
        discard(1);
        increment(counters_.parserErrors);
    }
    rx_[rxSize_++] = byte;
    for (uint8_t work = 0; work < MaxFrame && rxSize_; ++work) {
        if (rx_[0] != 0xa5 || (rxSize_ > 1 && rx_[1] != 0x5a)) {
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
        const uint8_t count = uint8_t(rx_[12] + 15);
        if (rxSize_ < count)
            break;
        if (load16(rx_ + count - 2) != crc16(rx_ + 2, uint8_t(count - 4))) {
            discard(1);
            increment(counters_.parserErrors);
            continue;
        }
        increment(counters_.usbFrames);
        dispatch(count, now);
        discard(count);
    }
}
bool ResponderEndpoint::enqueue(const uint8_t *bytes, uint8_t count) {
    if (uint16_t(txSize_) + count > TxCapacity)
        return false;
    for (uint8_t i = 0; i < count; ++i) {
        tx_[(uint16_t(txHead_) + txSize_) % TxCapacity] = bytes[i];
        ++txSize_;
    }
    return true;
}
void ResponderEndpoint::reply(uint8_t type, uint32_t session, uint32_t request, const uint8_t *bytes,
                              uint8_t count, bool cache) {
    if (count > MaxPayload) {
        increment(counters_.txOverflows);
        armed_ = initialized_ = false;
        port_.abortTx();
        return;
    }
    uint8_t frame[MaxFrame] = {0xa5, 0x5a, 1, type};
    store32(frame + 4, session);
    store32(frame + 8, request);
    frame[12] = count;
    memcpy(frame + 13, bytes, count);
    store16(frame + 13 + count, crc16(frame + 2, uint8_t(count + 11)));
    const uint8_t size = uint8_t(count + 15);
    if (!enqueue(frame, size)) {
        increment(counters_.txOverflows);
        armed_ = initialized_ = false;
        port_.abortTx();
        replySize_ = replyAt_ = 0;
        return;
    }
    if (cache) {
        memcpy(lastReply_, frame, size);
        lastReplySize_ = size;
    }
}
void ResponderEndpoint::ack(uint8_t command, uint8_t status, uint32_t session, uint32_t request, bool cache) {
    uint8_t data[AckSize] = {hd_bench::Version, 0, 0, 0, 0, command, status};
    store32(data + 1, generation_);
    reply(status == Ok ? Ack : Error, session, request, data, sizeof data, cache);
}
void ResponderEndpoint::requestQuiesce(uint32_t request) {
    armed_ = initialized_ = false;
    inputSize_ = 0;
    quiescing_ = true;
    quiesceRequest_ = request;
    if (replySize_ && !responseStarted_) {
        increment(counters_.canceled);
        replySize_ = replyAt_ = 0;
    }
}
void ResponderEndpoint::finishQuiesce() {
    replySize_ = replyAt_ = 0;
    responseStarted_ = quiescing_ = false;
    ++generation_;
    if (!generation_)
        ++generation_;
    // Clearing the local driver fault is permissible only at this explicit boundary.
    // It does not clear the bridge's RX; the coordinator must separately drain it.
    port_.clearFault();
    lineFaulted_ = false;
    ack(Quiesce, Ok, session_, quiesceRequest_);
    quiesceRequest_ = 0;
}
void ResponderEndpoint::dispatch(uint8_t count, uint32_t now) {
    const uint8_t command = rx_[3], size = rx_[12];
    const uint32_t session = load32(rx_ + 4), request = load32(rx_ + 8);
    const uint8_t *data = rx_ + 13;
    if (uint16_t(txSize_) + MaxFrame > TxCapacity) {
        increment(counters_.txOverflows);
        armed_ = initialized_ = false;
        port_.abortTx();
        replySize_ = replyAt_ = 0;
        txHead_ = txSize_ = lastReplySize_ = 0;
        ack(command, Overflow, session, request, false);
        return;
    }
    if (!session || !request) {
        ack(command, BadPayload, session, request, false);
        return;
    }
    if (command != Hello && command != Quiesce && command != Arm && command != Configure &&
        command != Diagnostics) {
        ack(command, BadCommand, session, request, false);
        return;
    }
    if (bound_ && session == session_ && request == lastId_) {
        if (count == lastRequestSize_ && memcmp(rx_, lastRequest_, count) == 0) {
            if (lastReplySize_)
                enqueue(lastReply_, lastReplySize_);
        } else
            ack(command, IdConflict, session, request, false);
        return;
    }
    if (command != Hello && (!bound_ || session != session_)) {
        ack(command, NotBound, session, request, false);
        return;
    }
    if (bound_ && session == session_ && request < lastId_) {
        ack(command, StaleRequest, session, request, false);
        return;
    }
    if (command == Hello &&
        (size != 2 || data[0] != hd_bench::Version || data[1] != hd_bench::PolicyVersion)) {
        ack(command, PolicyDenied, session, request, false);
        return;
    }
    if (quiescing_) {
        ack(command, Busy, session, request, false);
        return;
    }
    lastId_ = request;
    lastRequestSize_ = count;
    lastReplySize_ = 0;
    memcpy(lastRequest_, rx_, count);
    if (command == Hello) {
        // Rebinding disarms future requests. A started response is completed, never
        // hidden; only a following explicit QUIESCE acknowledges a clean generation.
        armed_ = initialized_ = false;
        session_ = session;
        bound_ = true;
        uint8_t info[MaxPayload] = {
            hd_bench::Version, hd_bench::PolicyVersion, BackendResponder, 1, ResponderCapabilities, 0};
        store32(info + 6, generation_);
        info[10] = quiescent() ? 0 : 1;
        info[11] = 1;
        info[14] = sizeof ResponderIdentity - 1;
        memcpy(info + 15, ResponderIdentity, sizeof ResponderIdentity - 1);
        reply(HelloInfo, session, request, info, uint8_t(15 + sizeof ResponderIdentity - 1));
        return;
    }
    if (!size || data[0] != hd_bench::PolicyVersion) {
        ack(command, PolicyDenied, session, request);
        return;
    }
    const uint8_t wanted = command == Configure ? 7 : command == Arm ? 5 : 1;
    if (size != wanted) {
        ack(command, BadPayload, session, request);
        return;
    }
    if (command == Quiesce) {
        requestQuiesce(request);
        tick(now);
        return;
    }
    if (command == Arm) {
        // A receiver error may arrive after QUIESCE ACK while the coordinator is
        // draining the physical line. Idle HIGH alone cannot clear that fault.
        if (port_.takeError()) {
            increment(counters_.lineErrors);
            lineFaulted_ = true;
        }
        if (lineFaulted_ || !generation_ || load32(data + 1) != generation_ || !quiescent() ||
            !port_.lineIdle(now) || port_.pendingRx()) {
            ack(command, NeedsNewExperiment, session, request);
            return;
        }
        armed_ = true;
        inputSize_ = 0;
        ack(command, Ok, session, request);
        return;
    }
    if (command == Configure) {
        if ((data[1] != 0 && data[1] != 1 && data[1] != 3) || data[2] > Trailing ||
            load16(data + 3) > 10000 || load16(data + 5) > 10000) {
            ack(command, BadPayload, session, request);
            return;
        }
        scenario_ = data[1];
        fault_ = data[2];
        delay_ = load16(data + 3);
        gap_ = load16(data + 5);
        ack(command, Ok, session, request);
        return;
    }
    uint8_t info[60] = {hd_bench::Version};
    store32(info + 1, generation_);
    info[5] = armed_;
    info[6] = quiescing_;
    info[7] = initialized_;
    info[8] = replySize_;
    info[9] = port_.pendingRx();
    store32(info + 10, counters_.requests);
    store32(info + 14, counters_.replies);
    store32(info + 18, counters_.canceled);
    store32(info + 22, counters_.lineErrors);
    store32(info + 26, counters_.parserErrors);
    store32(info + 30, port_.txBytes());
    store32(info + 34, counters_.txOverflows);
    port_.driverDiagnostics(info + 38, 22);
    reply(DiagnosticInfo, session, request, info, sizeof info);
}
void ResponderEndpoint::rawByte(uint8_t value, uint32_t observedAt) {
    if (!armed_)
        return;
    if (inputSize_ && uint32_t(observedAt - inputAt_) >= InterbyteTimeoutMs) {
        inputSize_ = 0;
        increment(counters_.parserErrors);
    }
    inputAt_ = observedAt;
    if (!inputSize_ && value != WakeBytes[0] && value != 0x20) {
        increment(counters_.parserErrors);
        return;
    }
    input_[inputSize_++] = value;
    if (input_[0] == WakeBytes[0]) {
        if (input_[inputSize_ - 1] != WakeBytes[inputSize_ - 1]) {
            inputSize_ = 0;
            increment(counters_.parserErrors);
            return;
        }
        if (inputSize_ == sizeof WakeBytes) {
            initialized_ = true;
            initAt_ = observedAt;
            inputSize_ = 0;
        }
        return;
    }
    if (inputSize_ < 5)
        return;
    inputSize_ = 0;
    if (!initialized_ || uint32_t(observedAt - initAt_) < InitWaitMs || replySize_ ||
        !TransactionEngine::allowed(input_, 5, uint8_t(input_[3] + 3))) {
        increment(counters_.parserErrors);
        return;
    }
    increment(counters_.requests);
    const uint8_t fault = fault_;
    fault_ = NoFault; // Faults are one-shot DLC replies; never poison a USB control ACK.
    if (fault == Silent)
        return;
    replySize_ = rawFixture(scenario_, input_[2], input_[3], reply_);
    replyAt_ = 0;
    responseStarted_ = false;
    replyAtMs_ = observedAt + 2u + (fault == Delay ? delay_ : 0u);
    activeGap_ = fault == Gap ? gap_ : 0;
    switch (fault) {
    case Header:
        reply_[0] = 0x7e;
        break;
    case Length:
        reply_[1] = 0xff;
        break;
    case Checksum:
        reply_[replySize_ - 1] ^= 1;
        break;
    case Truncated:
        --replySize_;
        break;
    case Noise:
        memmove(reply_ + 1, reply_, replySize_);
        reply_[0] = 0x7e;
        ++replySize_;
        break;
    case Trailing:
        reply_[replySize_++] = 0x7e;
        break;
    default:
        break;
    }
}
void ResponderEndpoint::tick(uint32_t now) {
    const uint8_t error = port_.takeError();
    if (error) {
        increment(counters_.lineErrors);
        lineFaulted_ = true;
        armed_ = initialized_ = false;
        port_.abortTx();
        replySize_ = replyAt_ = 0;
        responseStarted_ = false;
    }
    uint8_t value = 0;
    uint32_t observedAt = now;
    for (uint8_t work = 0; work < MaxDlcRx && port_.readByte(value, now, observedAt); ++work)
        rawByte(value, observedAt);
    if (replySize_ && (armed_ || responseStarted_) && int32_t(now - replyAtMs_) >= 0) {
        for (uint8_t work = 0; work < MaxDlcRx && replyAt_ < replySize_; ++work) {
            if (replyAt_ == 2 && activeGap_) {
                // The requested gap begins after the second physical stop bit,
                // not when its byte was merely copied into the TX queue.
                if (!port_.txIdle(now))
                    break;
                replyAtMs_ = now + activeGap_;
                activeGap_ = 0;
                break;
            }
            if (!port_.writeByte(reply_[replyAt_], now))
                break;
            responseStarted_ = true;
            ++replyAt_;
        }
        if (replyAt_ == replySize_ && port_.txIdle(now)) {
            increment(counters_.replies);
            replySize_ = replyAt_ = 0;
            responseStarted_ = false;
        }
    }
    if (quiescing_ && !replySize_ && port_.txIdle(now))
        finishQuiesce();
}
size_t ResponderEndpoint::read(uint8_t *output, size_t capacity) {
    const size_t count = capacity < txSize_ ? capacity : txSize_;
    for (size_t i = 0; i < count; ++i) {
        output[i] = tx_[txHead_];
        txHead_ = uint8_t((txHead_ + 1) % TxCapacity);
    }
    txSize_ = uint8_t(txSize_ - count);
    return count;
}
} // namespace hd_bench
