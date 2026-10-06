#include "transaction_engine.hpp"
#include <string.h>
namespace hd_bridge {
namespace {
const uint8_t Wake[] = {0x68, 0x6a, 0xf5, 0xaf, 0xbf, 0xb3, 0xb2, 0xc1, 0xdb, 0xb3, 0xe9};
uint16_t elapsed16(uint32_t elapsed) {
    return uint16_t(elapsed > 65535u ? 65535u : elapsed);
}
} // namespace
TransactionEngine::TransactionEngine(DlcPort &port) : port_(port) {
    newExperiment();
    state_ = NeedsExperiment;
}
void TransactionEngine::newExperiment() {
    memset(&result_, 0, sizeof result_);
    started_ = txDone_ = lastRx_ = 0;
    txSequenceAtStart_ = 0;
    state_ = ReadyForInit;
    txAt_ = txSize_ = expected_ = lateSize_ = lineError_ = 0;
    active_ = transmitting_ = complete_ = resultReady_ = false;
}
bool TransactionEngine::allowed(const uint8_t *request, uint8_t length, uint8_t expected) {
    if (length != 5 || request[0] != 0x20 || request[1] != 5 || expected != request[3] + 3)
        return false;
    if (!((request[2] == 0 && request[3] == 2) ||
          ((request[2] == 0x10 || request[2] == 0x14) && request[3] == 1)))
        return false;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < length; ++i)
        sum = uint8_t(sum + request[i]);
    return sum == 0;
}
void TransactionEngine::begin(uint8_t command, const uint8_t *bytes, uint8_t length, uint32_t now) {
    memset(&result_, 0, sizeof result_);
    result_.command = command;
    memcpy(result_.tx, bytes, length);
    started_ = now;
    txDone_ = lastRx_ = now;
    txSize_ = length;
    txAt_ = 0;
    txSequenceAtStart_ = port_.completedTxSequence();
    active_ = transmitting_ = true;
    complete_ = resultReady_ = false;
    state_ = command == Initialize ? Initializing : Executing;
}
bool TransactionEngine::initialize(uint32_t now) {
    if (state_ != ReadyForInit || active_ || resultReady_)
        return false;
    begin(Initialize, Wake, sizeof Wake, now);
    return true;
}
bool TransactionEngine::execute(const uint8_t *request, uint8_t length, uint8_t expected, uint32_t now) {
    if (state_ != Ready || active_ || resultReady_ || !allowed(request, length, expected))
        return false;
    begin(Execute, request, length, now);
    result_.address = request[2];
    result_.readLength = request[3];
    expected_ = expected;
    return true;
}
void TransactionEngine::finish(uint8_t status, uint32_t now) {
    if (status != Ok)
        port_.abortTx();
    result_.status = status;
    result_.txLength = txAt_;
    if (port_.hasPhysicalTxAccounting()) {
        const uint16_t complete = uint16_t(port_.completedTxSequence() - txSequenceAtStart_);
        result_.txLength = uint8_t(complete < txAt_ ? complete : txAt_);
    }
    result_.txElapsed = elapsed16(uint32_t((transmitting_ ? now : txDone_) - started_));
    result_.responseElapsed = transmitting_ ? 0 : elapsed16(uint32_t(now - txDone_));
    state_ = status == Ok ? Ready : Faulted;
    active_ = false;
    resultReady_ = true;
}
void TransactionEngine::abort(uint32_t now, uint8_t status) {
    port_.abortTx();
    if (active_)
        finish(status, now);
    else
        state_ = Faulted;
    // Neither this operation nor parser resets touch DlcPort's queued bytes.
}
void TransactionEngine::tick(uint32_t now) {
    const uint8_t portError = port_.takeError();
    if (portError) {
        if (active_)
            finish(portError, now);
        else {
            port_.abortTx();
            state_ = Faulted;
            lineError_ = portError;
        }
    }
    if (active_ && transmitting_ && uint32_t(now - started_) >= DlcTxTimeoutMs)
        finish(DlcTxTimeout, now);
    if (active_ && !transmitting_ && result_.command == Execute && !complete_) {
        if (uint32_t(now - txDone_) >= TotalTimeoutMs)
            finish(DlcTotalTimeout, now);
        else if (result_.rxLength && uint32_t(now - lastRx_) >= InterbyteTimeoutMs)
            finish(DlcInterbyteTimeout, now);
    }
    if (active_ && transmitting_) {
        for (uint8_t work = 0; work < 11 && txAt_ < txSize_; ++work) {
            if (!port_.writeByte(result_.tx[txAt_], now))
                break;
            ++txAt_;
        }
        if (txAt_ == txSize_ && port_.txIdle(now)) {
            transmitting_ = false;
            txDone_ = now;
        }
    }
    uint8_t byte = 0;
    uint32_t observedAt = now;
    for (uint8_t work = 0; work < MaxDlcRx && lateSize_ < MaxDlcRx && port_.readByte(byte, now, observedAt);
         ++work) {
        if (!active_) {
            late_[lateSize_++] = byte;
            if (state_ == Ready || state_ == ReadyForInit)
                state_ = Faulted;
            continue;
        }
        if (result_.rxLength == MaxDlcRx) {
            finish(Overflow, now);
            late_[lateSize_++] = byte;
            continue;
        }
        if (result_.rxLength) {
            const uint16_t gap = elapsed16(uint32_t(observedAt - lastRx_));
            if (gap > result_.maxGap)
                result_.maxGap = gap;
        }
        result_.rx[result_.rxLength++] = byte;
        lastRx_ = observedAt;
        if (transmitting_ || result_.command == Initialize) {
            finish(DlcUnexpected, now);
            continue;
        }
        if (complete_) {
            finish(DlcTrailing, now);
            continue;
        }
        if (result_.maxGap >= InterbyteTimeoutMs) {
            finish(DlcInterbyteTimeout, now);
            continue;
        }
        if (result_.rxLength == 1 && byte != 0) {
            finish(DlcHeader, now);
            continue;
        }
        if (result_.rxLength == 2 && byte != expected_) {
            finish(DlcLength, now);
            continue;
        }
        if (result_.rxLength == expected_) {
            uint8_t sum = 0;
            for (uint8_t i = 0; i < expected_; ++i)
                sum = uint8_t(sum + result_.rx[i]);
            if (sum)
                finish(DlcChecksum, now);
            else
                complete_ = true;
        }
    }
    // Keep the full response observation window before accepting a complete frame.
    // A second frame/noise in that window invalidates the entire operation.
    if (active_ && !transmitting_) {
        if (result_.command == Initialize && uint32_t(now - txDone_) >= InitWaitMs)
            finish(Ok, now);
        else if (complete_ && uint32_t(now - txDone_) >= TotalTimeoutMs)
            finish(Ok, now);
    }
}
bool TransactionEngine::takeResult(DlcResult &result) {
    if (!resultReady_)
        return false;
    result = result_;
    resultReady_ = false;
    return true;
}
uint8_t TransactionEngine::takeLate(uint8_t *output, uint8_t capacity) {
    const uint8_t count = lateSize_ < capacity ? lateSize_ : capacity;
    memcpy(output, late_, count);
    lateSize_ = uint8_t(lateSize_ - count);
    memmove(late_, late_ + count, lateSize_);
    return count;
}
} // namespace hd_bridge
