#ifndef HD_TEST_ONE_WIRE_LINE_HPP
#define HD_TEST_ONE_WIRE_LINE_HPP
#include "../../firmware/shared/one_wire.hpp"
#include <stdexcept>
namespace hd_test {
// Logical wired-AND event model. Not SPICE, an oscilloscope, or an AVR simulator.
class PairLine {
  public:
    hd_onewire::Driver first, second;
    explicit PairLine(uint32_t initial = 0)
        : now_(initial), totalTicks_(initial), high_(true), forcedLow_(false) {}
    uint32_t now() const { return now_; }
    uint32_t millis() const { return uint32_t(totalTicks_ / 2000u); }
    bool high() const { return high_; }
    void forceLow(bool value) { forcedLow_ = value; reconcile(); }
    void service() {
        // Both nodes observe the same level before simultaneously starting queued TX.
        first.service(now_, high_);
        second.service(now_, high_);
        reconcile();
    }
    void advanceTo(uint32_t target) {
        if (uint32_t(target - now_) >= 0x80000000u)
            throw std::runtime_error("PairLine advances must be below half the tick range");
        service();
        for (unsigned work = 0; ; ++work) {
            if (work > 2000000)
                throw std::runtime_error("PairLine bounded event budget exceeded");
            uint32_t delta = uint32_t(target - now_);
            bool event = false;
            if (first.hasEvent() && uint32_t(first.nextEvent() - now_) <= delta) {
                delta = uint32_t(first.nextEvent() - now_);
                event = true;
            }
            if (second.hasEvent() && uint32_t(second.nextEvent() - now_) <= delta) {
                delta = uint32_t(second.nextEvent() - now_);
                event = true;
            }
            if (!event) {
                totalTicks_ += uint32_t(target - now_);
                now_ = target;
                return;
            }
            now_ += delta;
            totalTicks_ += delta;
            const bool sampled = high_;
            first.timer(now_, sampled);
            second.timer(now_, sampled);
            reconcile();
        }
    }
  private:
    void reconcile() {
        first.outputApplied(now_);
        second.outputApplied(now_);
        const bool high = !forcedLow_ && !first.drivingLow() && !second.drivingLow();
        if (high_ && !high) {
            first.fallingEdge(now_);
            second.fallingEdge(now_);
        }
        high_ = high;
    }
    uint32_t now_;
    uint64_t totalTicks_;
    bool high_, forcedLow_;
};
class DriverPort {
  public:
    DriverPort(hd_onewire::Driver &driver, PairLine &line) : driver_(driver), line_(line) {}
    void begin() {}
    void service() { line_.service(); }
    bool sendByte(uint8_t value) { return driver_.enqueue(value); }
    bool txComplete() const { return driver_.txComplete(); }
    bool read(uint8_t &byte, uint32_t &observedAtMs) {
        hd_onewire::Received received;
        if (!driver_.pop(received))
            return false;
        byte = received.byte;
        observedAtMs = hd_onewire::observedMillis(received.ticks, line_.now(), line_.millis());
        return true;
    }
    void abort() { driver_.abort(); line_.service(); }
    hd_onewire::Error takeError() { return driver_.takeError(); }
    void clearFault() { driver_.clearFault(); }
    bool idleHigh() const { return line_.high() && driver_.txComplete() && !driver_.hasEvent(); }
    uint8_t rxPending() const { return driver_.rxPending(); }
    hd_onewire::Counters counters() const { return driver_.counters(); }
    uint16_t completedTxSequence() const { return driver_.completedTxSequence(); }
  private:
    hd_onewire::Driver &driver_;
    PairLine &line_;
};
}
#endif
