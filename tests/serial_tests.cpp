#include "transport/serial_transport.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// This QIODevice is injected into the production SerialTransport, so partial
// writes, queue bounds, callbacks and cancellation exercise its actual pump.
class ScriptDevice final : public QIODevice {
public:
    bool isSequential() const override { return true; }
    qint64 bytesToWrite() const override { return queued; }
    qint64 bytesAvailable() const override {
        return static_cast<qint64>(input.size()) + QIODevice::bytesAvailable() + (readFailure ? 1 : 0);
    }
    bool open(OpenMode mode) override {
        if (openFailure) { setErrorString("fixture access denied"); return false; }
        return QIODevice::open(mode | Unbuffered);
    }
    void inject(std::span<const std::uint8_t> bytes) {
        input.insert(input.end(), bytes.begin(), bytes.end());
        emit readyRead();
    }
    void drain(qint64 count) {
        count = std::min(count, queued);
        queued -= count;
        if (count) emit bytesWritten(count);
    }
    std::deque<qint64> writeResults;
    std::deque<std::uint8_t> input;
    std::vector<std::uint8_t> written;
    std::size_t writeCalls{}, maxOffer{};
    qint64 queued{}, peakQueued{};
    bool openFailure{}, readFailure{}, disconnectOnWrite{}, disconnectOnRead{};
protected:
    qint64 writeData(const char* data, qint64 count) override {
        ++writeCalls;
        maxOffer = std::max(maxOffer, static_cast<std::size_t>(count));
        if (disconnectOnWrite) { close(); return -1; }
        qint64 accepted = count;
        if (!writeResults.empty()) { accepted = writeResults.front(); writeResults.pop_front(); }
        if (accepted < 0) { setErrorString("fixture write error"); return -1; }
        accepted = std::min(accepted, count);
        written.insert(written.end(), data, data + accepted);
        queued += accepted;
        peakQueued = std::max(peakQueued, queued);
        return accepted;
    }
    qint64 readData(char* data, qint64 count) override {
        if (disconnectOnRead) { close(); return -1; }
        if (readFailure) { setErrorString("fixture read error"); return -1; }
        const auto size = std::min(static_cast<std::size_t>(count), input.size());
        for (std::size_t i = 0; i < size; ++i) { data[i] = static_cast<char>(input.front()); input.pop_front(); }
        return static_cast<qint64>(size);
    }
};

struct Fixture {
    hd::Time time{};
    QPointer<ScriptDevice> device;
    unsigned opened{}, sent{}, errors{};
    std::string lastError;
    std::vector<std::uint8_t> tx, rx, decoded;
    std::vector<char> order;
    std::size_t largestRx{};
    hd::SerialTransport transport{[this] { return time; }, [this] {
        auto next = std::make_unique<ScriptDevice>(); device = next.get(); return next;
    }};
    hd::TransportCallbacks callbacks() {
        return {
            [this](hd::Time) { ++opened; },
            [this](std::span<const std::uint8_t> bytes, hd::Time) {
                decoded.insert(decoded.end(), bytes.begin(), bytes.end()); order.push_back('D');
            },
            [this](hd::Time) { ++sent; },
            [this](const std::string& message, hd::Time) { ++errors; lastError = message; },
            [this](const std::string& kind, std::span<const std::uint8_t> bytes, hd::Time) {
                if (kind == "TX") tx.insert(tx.end(), bytes.begin(), bytes.end());
                else if (kind == "RX") {
                    rx.insert(rx.end(), bytes.begin(), bytes.end()); order.push_back('R');
                    largestRx = std::max(largestRx, bytes.size());
                }
            }
        };
    }
    void start() { transport.start(callbacks(), time); }
};

void queuedEvents() {
    QEventLoop loop;
    QTimer::singleShot(10, &loop, &QEventLoop::quit);
    loop.exec();
}

void partialWritesAndBounds() {
    Fixture f; f.start();
    require(f.opened == 1 && f.transport.isOpen(), "device opens once");
    std::vector<std::uint8_t> frame(256);
    for (std::size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<std::uint8_t>(i);
    f.device->writeResults = {3, 0, 2, 123, 128};
    require(f.transport.send(frame, 0), "accept bounded TX");
    f.transport.tick(1);
    require(f.tx.size() == 3 && f.transport.pendingBytes() == 256 && !f.sent, "raw TX means actually accepted bytes, not whole frame");
    f.transport.tick(2);
    require(f.device->writeCalls == 2 && f.tx.size() == 3, "zero write makes one attempt and preserves bytes");
    f.transport.tick(3);
    f.transport.tick(4);
    require(f.device->queued == 128 && f.tx.size() == 128, "Qt queue stops at 128");
    f.transport.tick(5);
    require(f.device->writeCalls == 4, "full Qt queue causes no additional write");
    f.device->drain(128);
    f.transport.tick(6);
    require(f.tx == frame && !f.sent, "local empty does not start response deadline until Qt drained");
    f.device->drain(128);
    f.transport.tick(7);
    require(f.sent == 1 && f.transport.pendingBytes() == 0 && f.errors == 0, "single completion after queues empty");
    require(f.device->peakQueued <= 128 && f.device->maxOffer <= 128, "bounded Qt writes");
}

void deadlinesAndFailures() {
    const std::array<std::uint8_t, 3> frame{1, 2, 3};
    {
        Fixture f; f.start(); f.device->writeResults = {0, 0, 0};
        require(f.transport.send(frame, 100), "send starts TX deadline");
        f.transport.tick(599);
        require(!f.errors && !f.sent, "TX stays pending before deterministic deadline");
        f.transport.tick(600);
        require(f.errors == 1 && !f.transport.isOpen() && f.transport.pendingBytes() == 0, "zero progress TX deadline clears state");
    }
    {
        Fixture f; f.start(); f.transport.send(frame, 0); f.transport.tick(0);
        f.transport.tick(500);
        require(f.errors == 1 && !f.sent, "Qt queue stalls share TX deadline");
    }
    {
        Fixture f; f.start(); f.device->writeResults = {-1};
        f.transport.send(frame, 0); f.transport.tick(1);
        require(f.errors == 1 && f.tx.empty() && !f.transport.isOpen(), "write error is visible and has no false TX");
    }
    {
        Fixture f; f.start(); f.device->disconnectOnWrite = true;
        f.transport.send(frame, 0); f.transport.tick(1);
        require(f.errors == 1 && !f.sent && !f.transport.isOpen(), "disconnect during write cancels signal stack safely");
    }
    {
        Fixture f; f.start(); std::vector<std::uint8_t> oversized(257, 3);
        require(!f.transport.send(oversized, 0) && f.errors == 1, "local queue overflow fails visibly");
    }
    {
        Fixture f; f.start(); f.transport.send(frame, 0);
        require(!f.transport.send(frame, 0) && f.errors == 1, "overlapping frame fails instead of interleaving");
    }
    {
        Fixture f; f.start(); f.transport.send(frame, 0); f.device->queued = 129; f.transport.tick(1);
        require(f.errors == 1 && !f.transport.isOpen(), "unexpected Qt queue overflow is fatal");
    }
    {
        Fixture f; f.start(); f.device->readFailure = true; f.device->inject({});
        require(f.errors == 1 && f.rx.empty(), "RX errors visible without fabricated bytes");
    }
    {
        Fixture f; f.start(); f.device->disconnectOnRead = true; f.device->inject(frame);
        require(f.errors == 1 && f.rx.empty() && f.decoded.empty() && !f.transport.isOpen(), "disconnect within read cannot deliver stale bytes");
    }
}

void callbackReconnection() {
    const std::array<std::uint8_t, 3> nextFrame{8, 7, 6};
    {
        Fixture f;
        auto callbacks = f.callbacks();
        const auto record = callbacks.raw;
        callbacks.raw = [&](const std::string& kind, std::span<const std::uint8_t> bytes, hd::Time time) {
            record(kind, bytes, time);
            f.start(); // replaces the device and callback set from inside raw TX
        };
        f.transport.start(std::move(callbacks), 0);
        const auto old = f.device;
        old->writeResults = {3};
        const std::vector<std::uint8_t> largeFrame(256, 9);
        f.transport.send(largeFrame, 0); f.transport.tick(1);
        require(f.opened == 2 && f.tx.size() == 3 && !f.sent && !f.errors, "raw TX callback reconnect cancels old remainder and completion");
        require(f.device != old && f.transport.pendingBytes() == 0, "raw TX reconnect owns a fresh empty device");
        f.transport.send(nextFrame, 1); f.transport.tick(2);
        f.device->drain(128); f.transport.tick(3); queuedEvents();
        require(f.sent == 1 && f.tx.size() == 6 && f.device->written == std::vector<std::uint8_t>(nextFrame.begin(), nextFrame.end()), "new TX remains independent of cancelled pump callbacks");
    }
    {
        Fixture f;
        auto callbacks = f.callbacks();
        callbacks.raw = [&](const std::string& kind, std::span<const std::uint8_t>, hd::Time) {
            if (kind == "RX") {
                f.start();
                f.device->inject(nextFrame);
            }
        };
        f.transport.start(std::move(callbacks), 0);
        const std::vector<std::uint8_t> oldData(3072, 99);
        f.device->inject(oldData); queuedEvents();
        require(f.decoded == std::vector<std::uint8_t>(nextFrame.begin(), nextFrame.end()), "raw RX reconnect cannot deliver old fragment using new callbacks");
        require(f.opened == 2 && !f.errors, "RX callback reconnect is stable");
    }
    {
        Fixture f;
        auto callbacks = f.callbacks();
        callbacks.sent = [&](hd::Time time) {
            ++f.sent;
            f.start();
            f.transport.send(nextFrame, time);
        };
        f.transport.start(std::move(callbacks), 0);
        f.transport.send(nextFrame, 0); f.transport.tick(1);
        f.device->drain(128); f.transport.tick(2);
        require(f.sent == 1 && f.opened == 2, "sent callback can reopen and queue a new frame");
        f.transport.tick(3); f.device->drain(128); f.transport.tick(4); queuedEvents();
        require(f.sent == 2 && !f.errors && f.transport.pendingBytes() == 0, "old completion cannot mutate replacement TX");
    }
    {
        Fixture f;
        auto callbacks = f.callbacks();
        callbacks.opened = [&](hd::Time) { ++f.opened; f.transport.close(); };
        f.transport.start(std::move(callbacks), 0); queuedEvents();
        require(f.opened == 1 && !f.transport.isOpen() && !f.errors, "close from opened callback leaves no hidden device");
    }
}

void boundedRxAndCancellation() {
    Fixture f; f.start();
    std::vector<std::uint8_t> bytes(3072);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i);
    f.device->inject(bytes);
    require(f.rx.size() == 1024, "one readyRead callback has 1024-byte work budget");
    queuedEvents();
    require(f.rx == bytes && f.decoded == bytes && f.largestRx <= 256, "RX continuations preserve all bytes in bounded chunks");
    for (std::size_t i = 0; i < f.order.size(); i += 2)
        require(f.order[i] == 'R' && f.order[i + 1] == 'D', "raw RX is logged before parsing each fragment");

    f.rx.clear(); f.decoded.clear(); f.order.clear();
    f.device->inject(bytes);
    const auto previous = f.device;
    const auto received = f.rx.size();
    const std::array<std::uint8_t, 3> tx{7, 8, 9};
    f.transport.send(tx, 0);
    f.transport.close(); f.start();
    queuedEvents();
    require(f.rx.size() == received && f.tx.empty() && !f.sent, "old read/write continuations cannot cross reconnect");
    require(previous.isNull() || previous != f.device, "reopen owns a fresh device");
    require(f.transport.isOpen() && f.opened == 2, "manual reconnect remains open");

    auto callbacks = f.callbacks();
    callbacks.raw = [&f](const std::string&, std::span<const std::uint8_t>, hd::Time) { f.transport.close(); };
    f.transport.start(std::move(callbacks), 1);
    const auto before = f.decoded.size();
    f.device->inject(tx);
    require(f.decoded.size() == before && !f.transport.isOpen(), "close from raw callback invalidates downstream delivery");

    Fixture fast; fast.start();
    fast.transport.send(tx, 0); fast.transport.tick(0);
    fast.device->drain(128); // bytesWritten schedules completion; no event loop yet.
    fast.device->inject(tx);
    require(fast.sent == 1 && fast.decoded.size() == tx.size(), "fast RX observes TX drain before deferred completion callback");
}

void timedRetryAndNativeOpenError() {
    Fixture f; f.start(); f.device->writeResults = {0, 0, 1, 2};
    QEventLoop loop;
    QTimer draining;
    draining.setInterval(1);
    QObject::connect(&draining, &QTimer::timeout, &loop, [&] {
        if (f.device) f.device->drain(128);
        if (f.sent || f.errors) loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    const std::array<std::uint8_t, 3> bytes{0, 5, 255};
    f.transport.send(bytes, 0); draining.start(); loop.exec();
    require(f.sent == 1 && !f.errors && f.device->writeCalls == 4, "event-loop timer retries zero/partial writes without spinning");
    require(f.tx == std::vector<std::uint8_t>(bytes.begin(), bytes.end()), "timer retries retain byte order");

    hd::SerialTransport serial([] { return hd::Time{0}; });
    unsigned errors = 0, opened = 0;
    hd::TransportCallbacks callbacks;
    callbacks.error = [&](const std::string& error, hd::Time) { require(!error.empty(), "native error text"); ++errors; };
    callbacks.opened = [&](hd::Time) { ++opened; };
#ifdef _WIN32
    serial.setPortName("COM65535");
#else
    serial.setPortName("/dev/hondadash-test-port-does-not-exist");
#endif
    serial.start(callbacks, 0);
    require(errors == 1 && !opened && !serial.isOpen(), "actual QSerialPort missing-port error");
    serial.start(callbacks, 1);
    require(errors == 2 && !opened && serial.pendingBytes() == 0, "native open error can be retried cleanly");
    serial.setPortName(""); serial.start(callbacks, 2);
    require(errors == 3 && !opened, "empty selection never scans or opens any port");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        partialWritesAndBounds();
        deadlinesAndFailures();
        boundedRxAndCancellation();
        callbackReconnection();
        timedRetryAndNativeOpenError();
        std::cout << "serial transport: partial/zero/error, bounded queues, RX order, cancellation and native open errors passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "serial transport failure: " << error.what() << '\n';
        return 1;
    }
}
