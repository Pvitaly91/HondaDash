#include "one_wire_avr.hpp"
#ifdef __AVR__
#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#if F_CPU != 16000000UL || !defined(__AVR_ATmega328P__)
#error "The M3a bench driver requires ATmega328P at 16 MHz."
#endif
namespace {
hd_onewire::Driver driver;
volatile uint16_t timerHigh;
bool inputHigh() { return (PINB & _BV(PB0)) != 0; }
uint32_t clockTicks() {
    uint16_t high = timerHigh;
    const uint16_t low = TCNT1;
    // Account for a hardware wrap pending its lower-priority overflow ISR.
    if ((TIFR1 & _BV(TOV1)) && low < 0x8000u)
        ++high;
    return (uint32_t(high) << 16) | low;
}
__attribute__((always_inline)) inline uint32_t gpioClockTicks() {
    // Called only with IRQ excluded. Latch TCNT1 immediately after PORTD;
    // reading software high afterward is safe because its ISR cannot run here.
    const uint16_t low = TCNT1;
    uint16_t high = timerHigh;
    if ((TIFR1 & _BV(TOV1)) && low < 0x8000u)
        ++high;
    return (uint32_t(high) << 16) | low;
}
void updateHardware() {
    if (driver.drivingLow())
        PORTD |= _BV(PD3);
    else
        PORTD &= uint8_t(~_BV(PD3));
    uint8_t mask = TIMSK1;
    if (!driver.captureNeeded()) {
        // Echo is sampled by COMPA. Data-bit edges are not new starts.
        mask &= uint8_t(~_BV(ICIE1));
    } else if (!(mask & _BV(ICIE1))) {
        // Arm before stop sampling. A next start captured during the stop ISR
        // remains pending; never clear an already enabled input capture flag.
        TIFR1 = _BV(ICF1);
        mask |= _BV(ICIE1);
    }
    if (driver.hasEvent()) {
        const uint16_t due = uint16_t(driver.nextEvent());
        // Never wait an entire 16-bit wrap if main/capture rearmed a past compare.
        const uint16_t now = TCNT1;
        OCR1A = int16_t(due - now) <= 4 ? uint16_t(now + 4) : due;
        TIFR1 = _BV(OCF1A);
        mask |= _BV(OCIE1A);
    } else
        mask &= uint8_t(~_BV(OCIE1A));
    TIMSK1 = mask;
}
void applyTxStart() {
    // Known successful beginTx: no RX/resync/capture and next compare is52us
    // ahead. This bounded path avoids the generic HAL's state/overdue branches.
    PORTD |= _BV(PD3);
    driver.startOutputApplied(gpioClockTicks());
    OCR1A = uint16_t(driver.nextEvent());
    TIFR1 = _BV(OCF1A);
    TIMSK1 = uint8_t((TIMSK1 & uint8_t(~_BV(ICIE1))) | _BV(OCIE1A));
}
}
ISR(TIMER1_OVF_vect) { ++timerHigh; }
ISR(TIMER1_CAPT_vect) {
    uint16_t high = timerHigh;
    const uint16_t captured = ICR1;
    if ((TIFR1 & _BV(TOV1)) && captured < 0x8000u)
        ++high;
    driver.fallingEdge((uint32_t(high) << 16) | captured);
    updateHardware();
}
ISR(TIMER1_COMPA_vect) {
    const bool high = inputHigh();
    const uint32_t now = clockTicks();
    driver.timer(now, high);
    if (driver.startNeedsAnchor()) {
        applyTxStart();
    } else {
        if (driver.stopNeedsAnchor()) {
            PORTD &= uint8_t(~_BV(PD3));
            driver.outputApplied(gpioClockTicks()); // Anchor after the physical stop edge.
        }
        updateHardware();
    }
}
namespace hd_onewire {
void AvrPort::begin() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        // External 47k base-emitter resistor holds the transistor off during reset.
        PORTD &= uint8_t(~_BV(PD3));
        DDRD |= _BV(PD3);
        DDRB &= uint8_t(~_BV(PB0));
        PORTB &= uint8_t(~_BV(PB0)); // No MCU pull-up to the bus/buffer.
        TCCR1A = 0;
        TCCR1B = _BV(CS11) | _BV(ICNC1); // /8, normal mode, falling capture, 4-cycle noise filter.
        TCNT1 = 0;
        timerHigh = 0;
        TIFR1 = _BV(ICF1) | _BV(OCF1A) | _BV(TOV1);
        TIMSK1 = _BV(ICIE1) | _BV(TOIE1);
        if (!inputHigh())
            driver.fallingEdge(0);
        updateHardware();
    }
}
void AvrPort::service() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (driver.service(clockTicks(), inputHigh())) {
            // service changed state only by beginning TX (LOW) or failing busy
            // (released). Avoid redundant phase tests in this atomic main path.
            if (driver.drivingLow())
                applyTxStart();
            else
                updateHardware();
        } // Active frames belong to ISR; never clear/rearm a pending compare here.
    }
}
bool AvrPort::sendByte(uint8_t byte) {
    bool result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        result = driver.enqueue(byte);
        // Overflow also releases the pin immediately.
        if (!result)
            updateHardware();
    }
    return result;
}
bool AvrPort::txComplete() const {
    bool result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { result = driver.txComplete(); }
    return result;
}
bool AvrPort::read(uint8_t &byte, uint32_t &observedAtMs) {
    Received received;
    bool result;
    uint32_t nowTicks = 0, nowMs = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        result = driver.pop(received);
        if (result) {
            nowTicks = clockTicks();
            nowMs = millis();
        }
    }
    if (result) {
        byte = received.byte;
        observedAtMs = observedMillis(received.ticks, nowTicks, nowMs);
    }
    return result;
}
void AvrPort::abort() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        driver.abort();
        updateHardware();
    }
}
Error AvrPort::takeError() {
    Error error;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { error = driver.takeError(); }
    return error;
}
void AvrPort::clearFault() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { driver.clearFault(); }
}
bool AvrPort::idleHigh() const {
    bool result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { result = inputHigh() && driver.txComplete() && !driver.hasEvent(); }
    return result;
}
uint8_t AvrPort::rxPending() const {
    uint8_t result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { result = driver.rxPending(); }
    return result;
}
Counters AvrPort::counters() const {
    Counters result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { result = driver.counters(); }
    return result;
}
uint16_t AvrPort::completedTxSequence() const {
    uint16_t result;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { result = driver.completedTxSequence(); }
    return result;
}
}
#endif
