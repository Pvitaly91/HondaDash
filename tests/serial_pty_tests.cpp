#include "application/session.hpp"
#include "transport/serial_transport.hpp"
#include "endpoint.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <pty.h>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Real kernel PTY master, with the same fixed-buffer Endpoint compiled for AVR.
// No desktop Emulator is present in this test path.
class FirmwarePty {
public:
    explicit FirmwarePty(std::function<hd::Time()> clock) : clock_(std::move(clock)) {
        int slave = -1;
        char name[256]{};
        require(::openpty(&master_, &slave, name, nullptr, nullptr) == 0, "openpty failed");
        path = name;
        ::close(slave);
        require(::fcntl(master_, F_SETFL, ::fcntl(master_, F_GETFL) | O_NONBLOCK) == 0, "PTY nonblocking setup failed");
        endpoint.reset(static_cast<std::uint32_t>(clock_()));
        timer_.setInterval(1);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { tick(); });
        timer_.start();
    }
    ~FirmwarePty() { disconnect(); }
    void disconnect() {
        timer_.stop();
        if (master_ >= 0) ::close(master_);
        master_ = -1;
    }
    void reset() {
        endpoint.reset(static_cast<std::uint32_t>(clock_()));
        pending_ = offset_ = 0;
        if (master_ >= 0) ::tcflush(master_, TCIOFLUSH);
    }
    std::string path, failure;
    hd_nano::Endpoint endpoint;
    std::size_t receivedBytes{}, sentBytes{}, fragments{};
private:
    void tick() {
        if (master_ < 0 || !failure.empty()) return;
        const auto now = static_cast<std::uint32_t>(clock_());
        std::array<std::uint8_t, 64> input{};
        // Bound each bridge callback, independently of the production RX limit.
        for (unsigned round = 0; round < 8; ++round) {
            const auto count = ::read(master_, input.data(), input.size());
            if (count <= 0) {
                // EIO is normal while the slave is closed between sessions.
                if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EIO && errno != EINTR)
                    failure = "PTY read: " + std::string(std::strerror(errno));
                break;
            }
            receivedBytes += static_cast<std::size_t>(count);
            for (ssize_t i = 0; i < count; ++i) endpoint.receive(input[static_cast<std::size_t>(i)], now);
        }
        endpoint.tick(now);
        if (pending_ == offset_) {
            // Changing 1/7/19 byte chunks spans headers, values and CRC fields.
            constexpr std::array<std::size_t, 3> chunks{1, 7, 19};
            pending_ = endpoint.read(output_.data(), chunks[fragments % chunks.size()]);
            offset_ = 0;
        }
        if (pending_ > offset_) {
            const auto count = ::write(master_, output_.data() + offset_, pending_ - offset_);
            if (count > 0) {
                offset_ += static_cast<std::size_t>(count);
                sentBytes += static_cast<std::size_t>(count);
                ++fragments;
            } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != EIO) {
                failure = "PTY write: " + std::string(std::strerror(errno));
            }
        }
    }
    std::function<hd::Time()> clock_;
    int master_{-1};
    QTimer timer_;
    std::array<std::uint8_t, 79> output_{};
    std::size_t pending_{}, offset_{};
};

void until(const std::function<bool()>& predicate, int timeout, const std::string& message,
           hd::Session& session, const std::function<hd::Time()>& clock, FirmwarePty& firmware) {
    if (predicate()) return;
    QEventLoop loop;
    QTimer ticker, guard;
    ticker.setInterval(2);
    guard.setSingleShot(true);
    QObject::connect(&ticker, &QTimer::timeout, &loop, [&] {
        session.tick(clock());
        if (predicate() || !firmware.failure.empty()) loop.quit();
    });
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    ticker.start(); guard.start(timeout); loop.exec();
    require(firmware.failure.empty(), firmware.failure);
    require(predicate(), message + "; session error=" + session.error());
}

void integration() {
    QElapsedTimer elapsed; elapsed.start();
    const std::function<hd::Time()> clock = [&] { return static_cast<hd::Time>(elapsed.elapsed()); };
    FirmwarePty firmware(clock);
    hd::SerialTransport serial(clock);
    serial.setPortName(firmware.path);
    std::uint32_t nextSession = 0x71350000;
    hd::Session session(serial, {}, {}, [&] { return ++nextSession; });
    hd::Parser transmitted;
    std::size_t helloCount = 0, rawRx = 0, rawTx = 0;
    std::vector<std::uint8_t> received;
    session.onRaw = [&](const hd::RawEvent& event) {
        if (event.kind == "RX") { rawRx += event.bytes.size(); received.insert(received.end(), event.bytes.begin(), event.bytes.end()); }
        if (event.kind == "TX") {
            rawTx += event.bytes.size();
            for (const auto& frame : transmitted.feed(event.bytes)) if (frame.type == hd::Hello) ++helloCount;
        }
    };
    session.start(clock());
    until([&] { return session.state() == hd::SessionState::Running && session.stats().accepted > 0; },
          8000, "real QSerialPort / PTY / embedded HELLO + snapshot", session, clock, firmware);
    require(session.deviceInfo().has_value(), "embedded identity/capabilities discovered through serial");
    require(helloCount >= 1 && rawTx > 0 && rawRx > 0 && firmware.fragments > 1, "actual OS byte path and fragmentation observed");

    const std::array<double, hd::ChannelCount> manual{1234., 42.5, 80., -12.3, 25., 100., 14.2};
    const auto beforeManual = session.model().current(hd::Channel::Rpm, clock());
    session.setScenario(3, 123);
    session.setManual(manual);
    require(session.model().current(hd::Channel::Rpm, clock()) == beforeManual, "control queue cannot directly update instruments");
    until([&] { return session.model().current(hd::Channel::Rpm, clock()) == manual[0]; },
          4000, "manual values reach production decoder", session, clock, firmware);
    for (std::size_t i = 0; i < manual.size(); ++i) {
        const auto value = session.model().current(static_cast<hd::Channel>(i), clock());
        require(value && std::abs(*value - manual[i]) < 0.00001, "known scaled manual channel " + std::to_string(i));
    }

    const auto corruptBefore = session.stats().corrupt;
    const auto rxBefore = rawRx;
    session.setFaults({false, 0, true, false});
    until([&] { return session.stats().corrupt > corruptBefore; }, 4000,
          "embedded corrupt snapshot crosses actual serial parser", session, clock, firmware);
    require(rawRx > rxBefore, "corrupt RX is retained in raw stream");
    const auto acceptedAfterCorrupt = session.stats().accepted;
    until([&] { return session.stats().accepted > acceptedAfterCorrupt; }, 3000,
          "parser recovers after corrupt serial snapshot", session, clock, firmware);

    session.setFaults({true, 0, false, false});
    until([&] { return session.model().channels()[0].quality == hd::Quality::Stale; }, 4000,
          "snapshot silence ages existing data", session, clock, firmware);
    const auto silentAccepted = session.stats().accepted;
    session.setFaults({false, 0, false, false});
    until([&] { return session.stats().accepted > silentAccepted && session.model().channels()[0].quality == hd::Quality::Valid; },
          4000, "fault restore command survives snapshot silence", session, clock, firmware);

    const auto helloBeforeReset = helloCount;
    const auto acceptedBeforeReset = session.stats().accepted;
    firmware.reset();
    until([&] { return helloCount > helloBeforeReset && session.stats().accepted > acceptedBeforeReset
                    && session.state() == hd::SessionState::Running; }, 8000,
          "firmware reset requires HELLO before new measurement", session, clock, firmware);

    const auto previousId = session.id();
    session.stop(clock());
    require(!serial.isOpen() && serial.pendingBytes() == 0, "stop closes OS device and clears queued TX");
    firmware.reset();
    session.start(clock());
    require(session.id() != previousId && !session.model().current(hd::Channel::Rpm, clock()), "reconnect clears old numbers and allocates new identity");
    until([&] { return session.state() == hd::SessionState::Running && session.stats().accepted > 0; },
          8000, "same explicitly selected PTY reconnects", session, clock, firmware);
    firmware.disconnect();
    until([&] { return session.state() == hd::SessionState::Faulted; }, 4000,
          "OS hangup becomes visible disconnected/fault state", session, clock, firmware);
    require(!serial.isOpen() && !session.error().empty(), "physical serial endpoint loss closes transport visibly");
    std::cout << "PTY verified: real QSerialPort <-> kernel PTY <-> embedded Endpoint; "
              << firmware.receivedBytes << " request bytes, " << firmware.sentBytes << " response bytes, "
              << helloCount << " HELLOs. This is not a USB-chip or physical Nano test.\n";
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try { integration(); return 0; }
    catch (const std::exception& error) { std::cerr << "serial PTY failure: " << error.what() << '\n'; return 1; }
}
