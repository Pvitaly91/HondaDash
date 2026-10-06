#include "one_wire.hpp"
#include <string.h>
namespace hd_onewire {
namespace {
void increment(uint16_t &value) {
    if (value != 65535u)
        ++value;
}
}
Driver::Driver()
    : next_(0), rxStart_(0), completedTxSequence_(0), txHead_(0), txSize_(0), rxHead_(0), rxSize_(0), txByte_(0), rxByte_(0),
      phase_(0), pendingError_(NoError), txActive_(false), rxActive_(false), resync_(false), low_(false),
      fault_(false), stopAnchored_(false) {
    memset(&counters_, 0, sizeof counters_);
}
void Driver::fail(Error error) {
    if (pendingError_ == NoError)
        pendingError_ = error;
    fault_ = true;
    txActive_ = false;
    txSize_ = 0;
    low_ = false;
}
bool Driver::enqueue(uint8_t byte) {
    if (fault_)
        return false;
    if (txSize_ == Capacity) {
        increment(counters_.txOverflow);
        fail(TxOverflow);
        return false;
    }
    tx_[uint8_t(txHead_ + txSize_) & (Capacity - 1)] = byte;
    ++txSize_;
    return true;
}
void Driver::beginTx(uint32_t now) {
    txByte_ = tx_[txHead_];
    txHead_ = uint8_t(txHead_ + 1) & (Capacity - 1);
    --txSize_;
    txActive_ = true;
    stopAnchored_ = false;
    low_ = true; // Start bit. GPIO enables an external open-collector transistor.
    phase_ = 1;
    next_ = now + HalfTicks;
}
bool Driver::service(uint32_t now, bool high) {
    if (fault_ || txActive_ || !txSize_)
        return false;
    if (rxActive_ || resync_ || !high) {
        increment(counters_.lineBusy);
        fail(LineBusy);
        return true;
    }
    beginTx(now);
    return true;
}
void Driver::fallingEdge(uint32_t now) {
    if (txActive_ && captureNeeded()) {
        if (phase_ == 20 && int32_t(now - next_) >= 0) {
            // The released stop bit and HIGH echo completed before this captured
            // peer start, even if CAPT outranks a pending completion COMPA.
            completeTx();
        } else {
            increment(counters_.collisions);
            fail(Collision);
            resync_ = true;
            rxStart_ = now;
            next_ = now + BitTicks;
            return;
        }
    }
    // Own echo is checked at every TX bit centre, never inserted into RX.
    if (txActive_ || rxActive_ || resync_)
        return;
    rxActive_ = true;
    rxByte_ = 0;
    phase_ = 0;
    rxStart_ = now;
    next_ = now + HalfTicks;
}
void Driver::completeTx() {
    ++completedTxSequence_;
    increment(counters_.txBytes);
    increment(counters_.echoBytes);
    txActive_ = false;
    low_ = false;
}
void Driver::pushRx(uint32_t now) {
    if (rxSize_ == Capacity) {
        increment(counters_.rxOverflow);
        fail(RxOverflow);
        return;
    }
    Received &value = rx_[uint8_t(rxHead_ + rxSize_) & (Capacity - 1)];
    value.byte = rxByte_;
    value.ticks = now;
    ++rxSize_;
    increment(counters_.rxBytes);
}
void Driver::timer(uint32_t now, bool high) {
    if (!hasEvent() || int32_t(now - next_) < 0)
        return;
    if (uint32_t(now - next_) > MaxLateTicks) {
        increment(counters_.timing);
        fail(Timing);
        rxActive_ = false;
        resync_ = !high;
        next_ = now + BitTicks;
        return;
    }
    if (txActive_) {
        if (phase_ & 1u) {
            if (high == low_) { // Includes inability to pull LOW and foreign dominant LOW.
                increment(counters_.collisions);
                fail(Collision);
                resync_ = !high;
                next_ = now + BitTicks;
                return;
            }
            ++phase_;
            next_ += HalfTicks;
            return;
        }
        if (phase_ == 20) {
            completeTx();
            // A complete stop bit elapsed, the bus is already released, and its echo was HIGH.
            if (txSize_)
                beginTx(next_);
            return;
        }
        low_ = phase_ == 18 ? false : (txByte_ & uint8_t(1u << (phase_ / 2 - 1))) == 0;
        ++phase_;
        next_ += HalfTicks;
        return;
    }
    if (resync_) {
        if (high)
            resync_ = false;
        else {
            if (uint32_t(now - rxStart_) >= uint32_t(12u * BitTicks)) {
                increment(counters_.stuckLow);
                fail(StuckLow);
                rxStart_ = now; // One bounded observation per 12 bits, saturating counter.
            }
            next_ += BitTicks;
        }
        return;
    }
    if (phase_ == 0) {
        if (high) {
            increment(counters_.falseStarts);
            fail(FalseStart);
            rxActive_ = false;
            return;
        }
    } else if (phase_ <= 8) {
        if (high)
            rxByte_ |= uint8_t(1u << (phase_ - 1));
    } else {
        rxActive_ = false;
        // Preserve the observed raw byte even on bad stop; the sticky error is consumed before RX.
        pushRx(now);
        if (!high) {
            increment(counters_.framing);
            fail(Framing);
            resync_ = true;
            next_ = rxStart_ + 12u * BitTicks;
        }
        return;
    }
    ++phase_;
    next_ += BitTicks;
}
bool Driver::pop(Received &received) {
    if (!rxSize_)
        return false;
    received = rx_[rxHead_];
    rxHead_ = uint8_t(rxHead_ + 1) & (Capacity - 1);
    --rxSize_;
    return true;
}
void Driver::abort() {
    // If this is an RX event, retain it: aborting our transmitter cannot stop the peer.
    txActive_ = false;
    txSize_ = 0;
    low_ = false;
}
void Driver::clearFault() {
    fault_ = false;
    pendingError_ = NoError;
}
Error Driver::takeError() {
    const Error result = pendingError_;
    pendingError_ = NoError;
    return result;
}
} // namespace hd_onewire
