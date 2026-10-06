#include "../firmware/nano_dlc_bridge_lab/endpoint.hpp"
#include "acceptance/runner.hpp"
#include "transport/serial_transport.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSerialPort>
#include <QTimer>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <pty.h>
#include <stdexcept>
#include <unistd.h>
#ifndef HD_DESKTOP_VERSION
#define HD_DESKTOP_VERSION "unknown"
#endif
#ifndef HD_DESKTOP_SHA
#define HD_DESKTOP_SHA "unknown"
#endif

namespace {
std::size_t assertions{};
void require(bool ok, const std::string &message) {
    ++assertions;
    if (!ok)
        throw std::runtime_error(message);
}
class FirmwarePty {
  public:
    explicit FirmwarePty(std::function<hd::Time()> clock) : clock_(std::move(clock)) {
        int slave = -1;
        char name[256]{};
        require(::openpty(&master_, &slave, name, nullptr, nullptr) == 0, "acceptance openpty");
        path = name;
        ::close(slave);
        require(::fcntl(master_, F_SETFL, ::fcntl(master_, F_GETFL) | O_NONBLOCK) == 0, "PTY nonblocking");
        endpoint.reset(deviceTime());
        timer_.setInterval(1);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] { tick(); });
        timer_.start();
    }
    ~FirmwarePty() {
        timer_.stop();
        if (master_ >= 0)
            ::close(master_);
    }
    std::string path, error;
    hd_bridge::BridgeEndpoint endpoint;
    std::size_t fragments{};

  private:
    std::uint32_t deviceTime() const { return 0xfffffff0u + static_cast<std::uint32_t>(clock_()); }
    void tick() {
        std::array<std::uint8_t, 64> input{};
        const auto now = deviceTime();
        for (unsigned round = 0; round < 8; ++round) {
            const auto count = ::read(master_, input.data(), input.size());
            if (count <= 0) {
                if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EIO && errno != EINTR)
                    error = std::strerror(errno);
                break;
            }
            for (ssize_t i = 0; i < count; ++i)
                endpoint.receive(input[static_cast<std::size_t>(i)], now);
        }
        endpoint.tick(now);
        if (pending_ == offset_) {
            constexpr std::array<std::size_t, 4> sizes{1, 3, 7, 19};
            pending_ = endpoint.read(output_.data(), sizes[fragments % sizes.size()]);
            offset_ = 0;
        }
        if (pending_ > offset_) {
            const auto count = ::write(master_, output_.data() + offset_, pending_ - offset_);
            if (count > 0) {
                offset_ += static_cast<std::size_t>(count);
                ++fragments;
            } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EIO && errno != EINTR)
                error = std::strerror(errno);
        }
    }
    std::function<hd::Time()> clock_;
    int master_{-1};
    QTimer timer_;
    std::array<std::uint8_t, 79> output_{};
    std::size_t pending_{}, offset_{};
};
void run(hd::acceptance::Runner &runner, const std::function<hd::Time()> &clock, int timeoutMs) {
    QEventLoop loop;
    QTimer tick, guard;
    tick.setInterval(2);
    tick.setTimerType(Qt::PreciseTimer);
    guard.setSingleShot(true);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        runner.tick(clock());
        if (runner.done())
            loop.quit();
    });
    bool wallTimeout = false;
    QObject::connect(&guard, &QTimer::timeout, &loop, [&] {
        wallTimeout = true;
        runner.cancel(clock());
        loop.quit();
    });
    runner.start(clock());
    if (!runner.done()) {
        tick.start();
        guard.start(timeoutMs);
        loop.exec();
    }
    require(!wallTimeout, "PTY runner real-time watchdog exceeded");
    require(runner.done(), "runner completed");
}
void integration(const std::filesystem::path &root) {
    using hd::acceptance::ExitCode;
    QElapsedTimer elapsed;
    elapsed.start();
    const std::function<hd::Time()> clock = [&] { return static_cast<hd::Time>(elapsed.elapsed()); };
    FirmwarePty peer(clock);
    hd::SerialTransport serial(clock);
    serial.setPortName(peer.path);
    hd::acceptance::Config config;
    config.backend = hd::acceptance::Backend::Serial;
    config.port = peer.path;
    config.reportDirectory = root / "production-PTY";
    config.desktopVersion = HD_DESKTOP_VERSION;
    config.desktopSha = HD_DESKTOP_SHA;
    config.os = "Linux PTY real-time; no physical Nano";
    config.bridgeSettings.bootMs = 20;
    hd::acceptance::Runner runner(serial, config);
    run(runner, clock, 45000);
    require(runner.exitCode() == ExitCode::Success, runner.message());
    require(peer.error.empty(), "PTY transport error: " + peer.error);
    require(peer.fragments > 100, "actual OS fragments through same runner");
    require(peer.endpoint.generation() == 2, "initial and explicit recovery experiments");
    require(!serial.isOpen(), "real serial transport closed after success");

    hd::SerialTransport missing(clock);
    config.port = "/dev/hondadash-no-such-device-m2c";
    config.reportDirectory = root / "missing-port";
    missing.setPortName(config.port);
    hd::acceptance::Runner absent(missing, config);
    run(absent, clock, 5000);
    require(absent.exitCode() == ExitCode::Endpoint, "missing port fails visibly");
    require(!missing.isOpen(), "missing port stays closed");

    FirmwarePty busyPeer(clock);
    QSerialPort holder(QString::fromStdString(busyPeer.path));
    require(holder.open(QIODevice::ReadWrite), "test obtains exclusive explicit PTY");
    hd::SerialTransport busy(clock);
    config.port = busyPeer.path;
    config.reportDirectory = root / "busy-port";
    busy.setPortName(config.port);
    hd::acceptance::Runner occupied(busy, config);
    run(occupied, clock, 5000);
    require(occupied.exitCode() == ExitCode::Endpoint, "busy port fails visibly");
    require(busyPeer.endpoint.dlcTxBytes() == 0, "busy-port failure cannot issue DLC");
    holder.close();
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        auto root = std::filesystem::current_path() / "acceptance-pty-reports";
        if (argc == 3 && std::string(argv[1]) == "--report-dir")
            root = std::filesystem::path(reinterpret_cast<const char8_t *>(argv[2]));
        else if (argc != 1)
            throw std::runtime_error("usage: acceptance_pty_tests [--report-dir directory]");
        integration(root);
        std::cout << assertions << " acceptance PTY assertions PASS (software peer, hardware NOT VERIFIED)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
