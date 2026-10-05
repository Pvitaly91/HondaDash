#include "transport/in_memory_transport.hpp"
#include "protocol/device_extension.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Harness {
    hd::InMemoryTransport transport;
    hd::Parser parser;
    std::vector<hd::Frame> frames;
    unsigned errors{};
    Harness() {
        hd::TransportCallbacks callbacks;
        callbacks.received = [&](auto bytes, hd::Time) {
            auto received = parser.feed(bytes);
            frames.insert(frames.end(), received.begin(), received.end());
        };
        callbacks.error = [&](const auto&, hd::Time) { ++errors; };
        transport.start(callbacks, 0);
        send(hd::Hello, 1, {}, 0);
        ticks(0, 30);
        check(frames.size() == 1 && frames[0].type == hd::HelloResponse, "HELLO");
        frames.clear();
    }
    void send(std::uint8_t type, std::uint32_t request, std::vector<std::uint8_t> payload, hd::Time now) {
        check(transport.send(hd::encode({type, 123, request, std::move(payload)}), now), "send accepted");
    }
    void ticks(hd::Time first, hd::Time last) {
        for (auto now = first; now <= last; ++now) transport.tick(now);
    }
};

void neverInterleave() {
    // Every possible overlap: before first fragment, during its tail, after it.
    for (hd::Time offset = 0; offset < 25; ++offset) {
        Harness h;
        h.transport.emulator().faults().delayMs = 400;
        h.send(hd::ReadSnapshot, 2, {}, 100);
        h.ticks(100, 490 + offset);
        h.send(hd::SetFaults, 3, {0,0,0,0}, 490 + offset);
        h.ticks(490 + offset, 550);
        check(h.frames.size() == 2, "delayed snapshot and concurrent restore ACK both arrive");
        bool snapshot = false, ack = false;
        for (const auto& frame : h.frames) {
            snapshot |= frame.type == hd::SnapshotResponse && frame.request == 2;
            ack |= frame.type == (hd::SetFaults | 0x80) && frame.request == 3 &&
                   frame.payload == std::vector<std::uint8_t>{0};
        }
        check(snapshot && ack && h.parser.errors() == 0, "frame bytes never interleave");
        check(h.errors == 0 && h.transport.pendingDeliveries() == 0, "stream drained cleanly");
    }
    Harness h;
    h.transport.emulator().faults().delayMs = 1000;
    h.send(hd::ReadSnapshot, 2, {}, 100);
    h.send(hd::GetDeviceInfo, 3, {}, 100);
    h.ticks(100, 140);
    check(h.frames.size() == 1 && h.frames[0].type == (hd::GetDeviceInfo | 0x80),
          "ACK does not wait behind future delayed frame");
    h.ticks(141, 1140);
    check(h.frames.size() == 2 && h.frames[1].type == hd::SnapshotResponse && h.parser.errors() == 0,
          "future snapshot remains intact");
}

void coalescedAndMalformed() {
    Harness h;
    auto bytes = hd::encode({hd::ReadSnapshot, 123, 2, {}});
    const auto next = hd::encode({hd::ReadSnapshot, 123, 3, {}});
    bytes.insert(bytes.end(), next.begin(), next.end());
    check(h.transport.send(bytes, 40), "coalesced TX");
    h.ticks(40, 90);
    check(h.frames.size() == 2 && h.frames[0].request == 2 && h.frames[1].request == 3 &&
          h.parser.errors() == 0, "coalesced requests produce complete successive frames");
    h.frames.clear();
    check(h.transport.send(hd::encode({hd::Hello, 999, 1000, {0}}), 100), "bad HELLO TX");
    h.ticks(100, 130);
    check(h.frames.size() == 1 && h.frames[0].type == hd::ErrorResponse &&
          h.frames[0].payload == std::vector<std::uint8_t>{2}, "bad foreign HELLO error2");
    h.frames.clear();
    h.send(hd::ReadSnapshot, 4, {}, 140);
    h.ticks(140, 180);
    check(h.frames.size() == 1 && h.frames[0].type == hd::SnapshotResponse,
          "bad HELLO did not change active identity or ordering");
}

void boundedOverflow() {
    Harness h;
    h.transport.emulator().faults().delayMs = 1000000;
    bool accepted = true;
    for (std::uint32_t request = 2; request < 1000 && accepted; ++request) {
        accepted = h.transport.send(hd::encode({hd::ReadSnapshot, 123, request, {}}), 100);
        check(h.transport.pendingDeliveries() <= 256, "bounded delivery storage");
    }
    check(!accepted && !h.transport.isOpen() && h.errors == 1 && h.transport.pendingDeliveries() == 0,
          "overflow visibly cancels transport without retaining fragments");
}
}
int main() {
    try {
        neverInterleave(); coalescedAndMalformed(); boundedOverflow();
        std::cout << "in-memory: atomic stream scheduling, HELLO ordering, bounded overflow PASS\n";
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
