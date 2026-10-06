#include "support/one_wire_line.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using hd_onewire::Driver;
using hd_onewire::Received;
unsigned checks = 0;
void check(bool condition, const char *expression, int line) {
    ++checks;
    if (!condition)
        throw std::runtime_error(std::to_string(line) + ": " + expression);
}
#define CHECK(...) check(bool((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
void allBytesAndEcho() {
    for (unsigned byte = 0; byte < 256; ++byte) {
        hd_test::PairLine line;
        CHECK(line.first.enqueue(uint8_t(byte)));
        line.service();
        CHECK(!line.high());
        line.advanceTo(Driver::BitTicks * 10 - 1);
        CHECK(!line.first.txComplete());
        line.advanceTo(Driver::BitTicks * 10);
        CHECK(line.first.txComplete() && line.high());
        Received received{};
        CHECK(line.second.pop(received) && received.byte == byte);
        CHECK(received.ticks == Driver::BitTicks * 9 + Driver::HalfTicks);
        CHECK(!line.first.pop(received));
        CHECK(line.first.counters().echoBytes == 1 && line.first.takeError() == hd_onewire::NoError);
        CHECK(line.first.completedTxSequence() == 1);
    }
}
void backToBackAndSampling() {
    hd_test::PairLine line;
    for (unsigned byte = 0; byte < Driver::Capacity; ++byte)
        CHECK(line.first.enqueue(uint8_t(byte * 17)));
    line.advanceTo(Driver::Capacity * 10 * Driver::BitTicks);
    CHECK(line.first.txComplete() && line.high());
    CHECK(line.second.rxPending() == Driver::Capacity);
    for (unsigned byte = 0; byte < Driver::Capacity; ++byte) {
        Received received{};
        CHECK(line.second.pop(received) && received.byte == byte * 17);
        CHECK(received.ticks == byte * 10 * Driver::BitTicks + 9 * Driver::BitTicks + Driver::HalfTicks);
    }
    hd_test::PairLine pattern;
    CHECK(pattern.first.enqueue(0xa5));
    pattern.service();
    CHECK(!pattern.high());
    for (unsigned bit = 0; bit < 8; ++bit) {
        pattern.advanceTo((bit + 1) * Driver::BitTicks);
        CHECK(pattern.high() == bool(0xa5 & (1 << bit)));
    }
    pattern.advanceTo(9 * Driver::BitTicks);
    CHECK(pattern.high() && !pattern.first.txComplete());
}
// External waveform supplies edges; receiver still uses the production state machine.
// Variable bit period models relative clock error. Jitter is on wire edges, late is ISR delay.
bool waveform(uint8_t byte, unsigned period, unsigned late, int edgeJitter) {
    Driver receiver;
    struct Edge { uint32_t at; bool high; };
    std::vector<Edge> edges{{100, false}};
    bool previous = false;
    for (unsigned bit = 0; bit < 9; ++bit) {
        const bool high = bit == 8 || (byte & (1u << bit));
        const int jitter = bit % 2 ? edgeJitter : -edgeJitter;
        if (high != previous)
            edges.push_back({uint32_t(100 + (bit + 1) * period + jitter), high});
        previous = high;
    }
    bool high = true;
    size_t edge = 0;
    for (unsigned work = 0; work < 40; ++work) {
        const uint32_t nextEdge = edge < edges.size() ? edges[edge].at : 0xffffffffu;
        const uint32_t nextSample = receiver.hasEvent() ? receiver.nextEvent() + late : 0xffffffffu;
        if (nextSample == 0xffffffffu && nextEdge == 0xffffffffu)
            break;
        if (nextEdge <= nextSample) {
            const Edge event = edges[edge++];
            if (high && !event.high)
                receiver.fallingEdge(event.at);
            high = event.high;
        } else
            receiver.timer(nextSample, high);
    }
    Received received{};
    return receiver.takeError() == hd_onewire::NoError && receiver.pop(received) && received.byte == byte;
}
void skewAndJitter() {
    for (unsigned byte = 0; byte < 256; ++byte) {
        CHECK(waveform(uint8_t(byte), 204, 0, 4));
        CHECK(waveform(uint8_t(byte), 212, 0, 4));
        CHECK(waveform(uint8_t(byte), 204, 40, 4));
        CHECK(waveform(uint8_t(byte), 212, 40, 4));
    }
    unsigned detected = 0;
    for (unsigned byte = 0; byte < 256; ++byte)
        detected += !waveform(uint8_t(byte), 250, 0, 0);
    CHECK(detected > 128); // UART cannot identify every out-of-budget waveform without a checksum.
    Driver late;
    late.fallingEdge(0);
    late.timer(Driver::HalfTicks + Driver::MaxLateTicks + 1, false);
    CHECK(late.takeError() == hd_onewire::Timing && late.faulted());
}
void errorsAndRecovery() {
    Driver falseStart;
    falseStart.fallingEdge(0);
    falseStart.timer(Driver::HalfTicks, true);
    CHECK(falseStart.takeError() == hd_onewire::FalseStart);
    CHECK(falseStart.rxPending() == 0 && falseStart.counters().falseStarts == 1);
    CHECK(!falseStart.enqueue(1));
    falseStart.clearFault();
    CHECK(falseStart.enqueue(1));

    hd_test::PairLine stuck;
    stuck.forceLow(true);
    stuck.advanceTo(10 * Driver::BitTicks);
    CHECK(stuck.second.takeError() == hd_onewire::Framing);
    Received raw{};
    CHECK(stuck.second.pop(raw) && raw.byte == 0); // Bad frame remains available for raw trace.
    stuck.advanceTo(12 * Driver::BitTicks);
    CHECK(stuck.second.takeError() == hd_onewire::StuckLow);
    CHECK(stuck.second.counters().stuckLow == 1 && !stuck.second.drivingLow());
    stuck.forceLow(false);
    stuck.advanceTo(13 * Driver::BitTicks);
    stuck.first.clearFault();
    stuck.second.clearFault();
    CHECK(stuck.first.enqueue(0xa5));
    stuck.advanceTo(23 * Driver::BitTicks);
    CHECK(stuck.second.pop(raw) && raw.byte == 0xa5);

    hd_test::PairLine conflict;
    CHECK(conflict.first.enqueue(0));
    CHECK(conflict.second.enqueue(255));
    conflict.advanceTo(2 * Driver::BitTicks);
    CHECK(conflict.second.takeError() == hd_onewire::Collision);
    CHECK(!conflict.second.drivingLow() && conflict.second.rxPending() == 0);
    conflict.advanceTo(15 * Driver::BitTicks);
    CHECK(conflict.high());

    Driver disconnectedDriver;
    CHECK(disconnectedDriver.enqueue(0));
    disconnectedDriver.service(0, true);
    disconnectedDriver.timer(Driver::HalfTicks, true); // Requested LOW never observed.
    CHECK(disconnectedDriver.takeError() == hd_onewire::Collision && !disconnectedDriver.drivingLow());

    Driver busy;
    CHECK(busy.enqueue(0));
    busy.service(0, false);
    CHECK(busy.takeError() == hd_onewire::LineBusy && !busy.drivingLow());
}
void overflowAbortAndWrap() {
    Driver tx;
    for (unsigned byte = 0; byte < Driver::Capacity; ++byte)
        CHECK(tx.enqueue(uint8_t(byte)));
    CHECK(!tx.enqueue(17));
    CHECK(tx.takeError() == hd_onewire::TxOverflow && tx.txComplete() && !tx.drivingLow());
    hd_test::PairLine rx;
    for (unsigned byte = 0; byte < Driver::Capacity + 1; ++byte) {
        CHECK(rx.first.enqueue(uint8_t(byte)));
        rx.advanceTo(rx.now() + 10 * Driver::BitTicks);
    }
    CHECK(rx.second.takeError() == hd_onewire::RxOverflow);
    CHECK(rx.second.rxPending() == Driver::Capacity);
    CHECK(rx.second.counters().rxOverflow == 1);
    rx.second.abort();
    CHECK(rx.second.rxPending() == Driver::Capacity); // Stop never deletes late/raw RX.
    hd_test::PairLine aborted;
    CHECK(aborted.first.enqueue(0));
    aborted.service();
    CHECK(!aborted.high());
    aborted.first.abort();
    aborted.service();
    CHECK(aborted.high() && aborted.first.txComplete());

    hd_test::PairLine wrapped(0xfffffe00u);
    const uint32_t start = wrapped.now();
    CHECK(wrapped.first.enqueue(0x69));
    wrapped.advanceTo(start + 10 * Driver::BitTicks);
    Received value{};
    CHECK(wrapped.second.pop(value) && value.byte == 0x69);
    CHECK(value.ticks == uint32_t(start + 9 * Driver::BitTicks + Driver::HalfTicks));
    CHECK(hd_onewire::observedMillis(0xfffffff0u, 0x100u, 1234) == 1233);
    CHECK(hd_onewire::observedMillis(100, 4100, 1) == 0xffffffffu); // Engine millis wrap.
    CHECK(sizeof(Driver) <= 240); // AVR is smaller: host Received padding differs.
}
void captureArmingAndDeferredInterrupt() {
    Driver receiver;
    receiver.fallingEdge(0);
    CHECK(!receiver.captureNeeded());
    for (unsigned sample = 0; sample < 9; ++sample) {
        receiver.timer(receiver.nextEvent() + Driver::MaxLateTicks, sample != 0); // Receive FF.
        CHECK(receiver.captureNeeded() == (sample == 8));
    }
    // Fast peer (204 ticks/bit) starts its second frame while our late stop ISR
    // is still executing. This models retained ICR/ICF with IRQ dispatch deferred.
    const uint32_t secondStart = 2040;
    const uint32_t stopSample = receiver.nextEvent() + Driver::MaxLateTicks;
    const uint32_t isrReturn = stopSample + 50; // 25us ISR tail crosses next start.
    CHECK(stopSample < secondStart && secondStart < isrReturn);
    const bool captureEnabledBeforeStop = receiver.captureNeeded();
    receiver.timer(stopSample, true);
    const bool hardwareLatched = captureEnabledBeforeStop && secondStart < isrReturn;
    CHECK(hardwareLatched && receiver.captureNeeded()); // HAL must not clear already-enabled ICF.
    receiver.fallingEdge(secondStart); // ISR runs at isrReturn; ICR preserves actual edge time.
    CHECK(receiver.nextEvent() > isrReturn && !receiver.captureNeeded());
    for (unsigned sample = 0; sample < 10; ++sample) {
        const uint32_t at = receiver.nextEvent();
        const unsigned bit = (at - secondStart) / 204;
        const bool high = bit >= 9 || (bit > 0 && (0xa5 & (1u << (bit - 1))));
        receiver.timer(at, high);
    }
    Received received{};
    CHECK(receiver.pop(received) && received.byte == 255 && received.ticks == stopSample);
    CHECK(receiver.pop(received) && received.byte == 0xa5);
    CHECK(receiver.takeError() == hd_onewire::NoError);

    for (bool early : {false, true}) {
        Driver sender;
        CHECK(sender.enqueue(0xa5));
        sender.service(0, true);
        while (sender.nextEvent() < 10 * Driver::BitTicks)
            sender.timer(sender.nextEvent(), !sender.drivingLow());
        CHECK(sender.captureNeeded() && !sender.txComplete());
        // CAPT has higher priority than a pending COMPA at the same completion tick.
        sender.fallingEdge(10 * Driver::BitTicks - (early ? 1 : 0));
        if (early) {
            CHECK(sender.takeError() == hd_onewire::Collision);
            CHECK(sender.completedTxSequence() == 0 && !sender.drivingLow());
        } else {
            CHECK(sender.takeError() == hd_onewire::NoError && sender.completedTxSequence() == 1);
            CHECK(sender.txComplete() && sender.nextEvent() == 10 * Driver::BitTicks + Driver::HalfTicks);
        }
    }
    Driver released;
    CHECK(released.enqueue(0xa5));
    CHECK(released.service(0, true));
    CHECK(!released.service(1, false)); // HAL must leave active compare registers alone.
    while (released.nextEvent() <= 9 * Driver::BitTicks)
        released.timer(released.nextEvent(), !released.drivingLow());
    CHECK(released.stopNeedsAnchor() && !released.drivingLow());
    const uint32_t actualRelease = 9 * Driver::BitTicks + 32; // 16us GPIO/ISR output delay.
    released.outputApplied(actualRelease);
    CHECK(released.nextEvent() == actualRelease + Driver::HalfTicks);
    released.timer(released.nextEvent(), true);
    released.timer(actualRelease + Driver::BitTicks - 1, true);
    CHECK(!released.txComplete());
    released.timer(actualRelease + Driver::BitTicks, true);
    CHECK(released.txComplete() && released.completedTxSequence() == 1);
}
}
int main() {
    try {
        allBytesAndEcho();
        backToBackAndSampling();
        skewAndJitter();
        errorsAndRecovery();
        overflowAbortAndWrap();
        captureArmingAndDeferredInterrupt();
        std::cout << "one_wire: " << checks << " assertions PASS; bit-level model, physical timing NOT VERIFIED\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "one_wire failure: " << e.what() << '\n';
        return 1;
    }
}
