#include "../firmware/nano_dlc_bridge_lab/endpoint.hpp"
#include "bridge/client.hpp"
#include "honda_dlc/session.hpp"
#include "transport/serial_transport.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <pty.h>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>

namespace {
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
class FirmwarePty {
  public:
    explicit FirmwarePty(std::function<hd::Time()> clock) : clock_(std::move(clock)) {
        int slave = -1;
        char name[256]{};
        require(::openpty(&master_, &slave, name, nullptr, nullptr) == 0, "bridge openpty failed");
        path = name;
        ::close(slave);
        require(::fcntl(master_, F_SETFL, ::fcntl(master_, F_GETFL) | O_NONBLOCK) == 0,
                "bridge PTY nonblocking");
        reset();
        timer_.setInterval(1);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { tick(); });
        timer_.start();
    }
    ~FirmwarePty() { disconnect(); }
    void disconnect() {
        timer_.stop();
        if (master_ >= 0)
            ::close(master_);
        master_ = -1;
    }
    void reset() {
        endpoint.reset(deviceTime());
        pending_ = offset_ = 0;
        if (master_ >= 0)
            ::tcflush(master_, TCIOFLUSH);
    }
    std::string path, failure;
    hd_bridge::BridgeEndpoint endpoint;
    std::size_t receivedBytes{}, sentBytes{}, fragments{};

  private:
    std::uint32_t deviceTime() const {
        return 0xfffffff0u + static_cast<std::uint32_t>(clock_()); // unrelated/wrapping epoch
    }
    void tick() {
        if (master_ < 0 || !failure.empty())
            return;
        const auto now = deviceTime();
        std::array<std::uint8_t, 64> input{};
        for (unsigned round = 0; round < 8; ++round) {
            const auto count = ::read(master_, input.data(), input.size());
            if (count <= 0) {
                if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EIO && errno != EINTR)
                    failure = "bridge PTY read: " + std::string(std::strerror(errno));
                break;
            }
            receivedBytes += static_cast<std::size_t>(count);
            for (ssize_t i = 0; i < count; ++i)
                endpoint.receive(input[static_cast<std::size_t>(i)], now);
        }
        endpoint.tick(now);
        if (pending_ == offset_) {
            constexpr std::array<std::size_t, 4> chunks{1, 3, 7, 19};
            pending_ = endpoint.read(output_.data(), chunks[fragments % chunks.size()]);
            offset_ = 0;
        }
        if (pending_ > offset_) {
            const auto count = ::write(master_, output_.data() + offset_, pending_ - offset_);
            if (count > 0) {
                offset_ += count;
                sentBytes += count;
                ++fragments;
            } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != EIO)
                failure = "bridge PTY write: " + std::string(std::strerror(errno));
        }
    }
    std::function<hd::Time()> clock_;
    int master_{-1};
    QTimer timer_;
    std::array<std::uint8_t, 79> output_{};
    std::size_t pending_{}, offset_{};
};
void until(const std::function<bool()> &predicate, int timeout, const std::string &what,
           hd::dlc::Session &session, hd::bridge::Client &client, const std::function<hd::Time()> &clock,
           FirmwarePty &firmware) {
    if (predicate())
        return;
    QEventLoop loop;
    QTimer ticker, guard;
    ticker.setInterval(2);
    guard.setSingleShot(true);
    QObject::connect(&ticker, &QTimer::timeout, &loop, [&] {
        session.tick(clock());
        if (predicate() || !firmware.failure.empty())
            loop.quit();
    });
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    ticker.start();
    guard.start(timeout);
    loop.exec();
    require(firmware.failure.empty(), firmware.failure);
    require(predicate(), what + "; session=" + session.error() + "; bridge=" + client.error());
}
void integration() {
    QElapsedTimer elapsed;
    elapsed.start();
    const std::function<hd::Time()> clock = [&] { return static_cast<hd::Time>(elapsed.elapsed()); };
    FirmwarePty firmware(clock);
    hd::SerialTransport serial(clock);
    serial.setPortName(firmware.path);
    hd::bridge::Settings settings;
    settings.bootMs = 20;
    hd::bridge::Client client(serial, settings);
    hd::dlc::Session session(client);
    std::size_t usbTx = 0, usbRx = 0, dlcTx = 0, dlcRx = 0;
    session.onRaw = [&](const hd::RawEvent &event) {
        if (event.kind == "usb_tx")
            usbTx += event.bytes.size();
        if (event.kind == "usb_rx")
            usbRx += event.bytes.size();
        if (event.kind == "bridge_dlc_tx")
            dlcTx += event.bytes.size();
        if (event.kind == "bridge_dlc_rx")
            dlcRx += event.bytes.size();
    };
    client.connect(clock());
    until([&] { return client.state() == hd::bridge::State::Ready; }, 5000,
          "bridge serial identity handshake", session, client, clock, firmware);
    require(firmware.endpoint.dlcTxBytes() == 0, "handshake must not send DLC initialization");
    require(client.info() && client.info()->backend == 1 && !client.info()->physicalDlcEnabled,
            "explicit virtual-only bridge identity");
    session.start(clock());
    until(
        [&] {
            return session.model().current(hd::Channel::Rpm, clock()) == 750 &&
                   session.model().current(hd::Channel::Coolant, clock()) == 61 &&
                   session.model().current(hd::Channel::Throttle, clock()) == 32;
        },
        8000, "raw A through QSerialPort/PTY/embedded bridge/decoder", session, client, clock, firmware);
    session.setScenario(hd::dlc::Scenario::Higher);
    until(
        [&] {
            return session.model().current(hd::Channel::Rpm, clock()) == 1500 &&
                   session.model().current(hd::Channel::Coolant, clock()) == 89 &&
                   session.model().current(hd::Channel::Throttle, clock()) == 75;
        },
        6000, "raw B through QSerialPort/PTY/embedded bridge/decoder", session, client, clock, firmware);
    require(usbTx && usbRx && dlcTx && dlcRx && firmware.fragments > 20,
            "both byte layers and OS fragmentation logged");
    hd::dlc::Faults fault;
    fault.corruptNext = true;
    session.setFaults(fault);
    until([&] { return session.state() == hd::dlc::State::Faulted; }, 5000, "DLC checksum fault", session,
          client, clock, firmware);
    require(client.diagnostics().status == hd_bridge::DlcChecksum,
            "valid USB CRC preserves invalid inner checksum fact");
    until([&] { return session.model().channels()[0].quality == hd::Quality::Stale; }, 2500,
          "faulted values age", session, client, clock, firmware);
    session.setFaults({});
    session.start(clock());
    until([&] { return session.stats().accepted >= 3; }, 6000, "explicit new virtual experiment", session,
          client, clock, firmware);
    firmware.reset();
    until([&] { return session.state() == hd::dlc::State::Faulted; }, 5000, "reset rejects old operation",
          session, client, clock, firmware);
    require(!client.info(), "reset invalidates handshake, no silent resume");
    session.stop(clock());
    client.disconnect(clock());
    require(!serial.isOpen(), "disconnect closes selected OS device");
    client.connect(clock());
    until([&] { return client.state() == hd::bridge::State::Ready; }, 5000, "explicit reconnect handshake",
          session, client, clock, firmware);
    require(session.model().channels()[0].quality == hd::Quality::NoData,
            "reconnect cannot renew old measurements");
    session.start(clock());
    until([&] { return session.stats().accepted >= 3; }, 6000, "new experiment after reconnect", session,
          client, clock, firmware);
    firmware.disconnect();
    until([&] { return session.state() == hd::dlc::State::Faulted; }, 5000, "OS endpoint loss visible",
          session, client, clock, firmware);
    require(!serial.isOpen(), "serial loss closes transport without fallback");
    std::cout << "Bridge PTY PASS: real QSerialPort <-> kernel PTY <-> same C++11 bridge/virtual ECU, "
              << usbTx << " TX / " << usbRx << " RX USB bytes; " << dlcTx << " TX / " << dlcRx
              << " RX reported DLC bytes. No physical Nano/USB chip/DLC test.\n";
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        integration();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
