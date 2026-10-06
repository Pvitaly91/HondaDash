#ifndef HD_ONE_WIRE_HPP
#define HD_ONE_WIRE_HPP
#include <stdint.h>

// Independent of Arduino: this exact state machine is compiled for AVR and hosts.
namespace hd_onewire {
enum Error : uint8_t {
    NoError = 0, FalseStart, Framing, RxOverflow, TxOverflow, StuckLow, Collision, Timing, LineBusy
};
struct Received {
    uint8_t byte;
    uint32_t ticks; // Actual stop-bit sample, 0.5 us device ticks, modulo 2^32.
};
struct Counters {
    uint16_t rxBytes, txBytes, echoBytes, falseStarts, framing, rxOverflow, txOverflow;
    uint16_t stuckLow, collisions, timing, lineBusy;
};
class Driver {
  public:
    static const uint8_t Capacity = 16;
    static const uint16_t BitTicks = 208, HalfTicks = 104, MaxLateTicks = 40;
    // Project electrical acceptance allocation: D3-release to actual DATA HIGH
    // is at most8us. 17 ticks give >=8.33us at the -2% clock corner.
    // Extending our own stop does not change a bit/sample period.
    static const uint16_t StopSettleTicks = 17;
    Driver();
    bool enqueue(uint8_t byte);
    // Main context starts queued TX. Call with interrupts excluded on AVR.
    bool service(uint32_t now, bool high); // true only if TX/hardware state changed.
    // Hardware input capture supplies the actual falling-edge timestamp.
    void fallingEdge(uint32_t now);
    // One bounded event, never a catch-up loop. high was sampled on ISR entry.
    void timer(uint32_t now, bool high);
    bool pop(Received &received);
    void abort(); // Cancel/release TX; retain RX and fault diagnostics.
    void clearFault(); // Only after an explicitly coordinated new experiment.
    Error takeError();
    bool faulted() const { return fault_; }
    bool txComplete() const { return !txActive_ && txSize_ == 0; }
    bool drivingLow() const { return low_; }
    bool startNeedsAnchor() const { return txActive_ && phase_ == 1 && !startAnchored_; }
    // HAL-only fast path after startNeedsAnchor() and the actual GPIO write.
    void startOutputApplied(uint32_t now) {
        next_ = now + HalfTicks;
        startAnchored_ = true;
    }
    bool stopNeedsAnchor() const { return txActive_ && phase_ == 19 && !stopAnchored_; }
    // HAL invokes after applying D3. Allow frontend release settling before the
    // full own stop duration. A spec-compliant fast peer may start earlier than
    // our conservative own completion; its captured edge has a separate deadline.
    void outputApplied(uint32_t now) {
        if (startNeedsAnchor()) {
            startOutputApplied(now);
        } else if (stopNeedsAnchor()) {
            stopEarliestPeer_ = now + BitTicks;
            next_ = now + HalfTicks + StopSettleTicks;
            stopAnchored_ = true;
        }
    }
    bool hasEvent() const { return txActive_ || rxActive_ || resync_; }
    // Arm before stop processing can overlap the next frame's falling start.
    bool captureNeeded() const {
        if (resync_)
            return false;
        if (txActive_)
            return phase_ >= 19 && txSize_ == 0;
        return !rxActive_ || phase_ >= 9;
    }
    uint32_t nextEvent() const { return next_; }
    uint8_t rxPending() const { return rxSize_; }
    uint8_t txPending() const { return uint8_t(txSize_ + (txActive_ ? 1 : 0)); }
    uint16_t completedTxSequence() const { return completedTxSequence_; }
    const Counters &counters() const { return counters_; }
  private:
    void fail(Error error);
    void beginTx(uint32_t now);
    void completeTx();
    void pushRx(uint32_t now);
    uint8_t tx_[Capacity];
    Received rx_[Capacity];
    uint32_t next_, rxStart_, stopEarliestPeer_;
    Counters counters_;
    uint16_t completedTxSequence_;
    uint8_t txHead_, txSize_, rxHead_, rxSize_, txByte_, rxByte_, phase_;
    Error pendingError_;
    bool txActive_, rxActive_, resync_, low_, fault_, stopAnchored_, startAnchored_;
};
// Map a captured MCU timestamp into the engine's modulo-2^32 milliseconds.
// Caller snapshots both clocks together; queued age must be below 2^31 ticks.
inline uint32_t observedMillis(uint32_t captured, uint32_t nowTicks, uint32_t nowMs) {
    const uint32_t age = uint32_t(nowTicks - captured);
    return uint32_t(nowMs - age / 2000u - (age % 2000u != 0 ? 1u : 0u));
}
} // namespace hd_onewire
#endif
