#include "acceptance/runner.hpp"
#include "bridge/native_transport.hpp"
#ifdef HONDADASH_WITH_SERIAL
#include "transport/serial_transport.hpp"
#endif
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSysInfo>
#include <QTimer>
#include <csignal>
#include <iostream>
#include <memory>
#include <set>

#ifndef HD_DESKTOP_VERSION
#define HD_DESKTOP_VERSION "unknown"
#endif
#ifndef HD_DESKTOP_SHA
#define HD_DESKTOP_SHA "unknown"
#endif
namespace {
volatile std::sig_atomic_t cancelled = 0;
void signalHandler(int) {
    cancelled = 1;
}
void usage() {
    std::cout << "HondaDashBridgeCheck --backend native --report <directory>\n"
                 "HondaDashBridgeCheck --backend serial --port <explicit-port> --report <directory>\n"
                 "HondaDashBridgeCheck --backend two-nano-bench --bridge-port <PORT_A> --responder-port "
                 "<PORT_B> --report <directory>\n"
                 "Runs the selected bridge handshake, A/B, 10 s freshness observation, checksum fault,\n"
                 "Stale/hide/no-polling and explicit recovery. No upload or port enumeration.\n"
                 "A serial PASS does not verify physical Nano, USB reset/unplug, or electrical DLC.\n";
}
std::filesystem::path path(const QString &value) {
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    const auto utf8 = value.toUtf8();
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.constData()),
                                               static_cast<std::size_t>(utf8.size())));
#endif
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    hd::acceptance::Config config;
    config.desktopVersion = HD_DESKTOP_VERSION;
    config.desktopSha = HD_DESKTOP_SHA;
    config.os = (QSysInfo::prettyProductName() + " / " + QSysInfo::currentCpuArchitecture()).toStdString();
    std::set<QString> keys;
    QString backend, port, responderPort, report;
    for (qsizetype i = 1; i < args.size(); ++i) {
        const auto &key = args[i];
        if (key == "--help" || key == "-h") {
            usage();
            return 0;
        }
        if ((key != "--backend" && key != "--port" && key != "--bridge-port" && key != "--responder-port" &&
             key != "--report") ||
            i + 1 >= args.size() || !keys.insert(key).second) {
            std::cerr << "Invalid, duplicate or missing option: " << key.toStdString() << '\n';
            usage();
            return static_cast<int>(hd::acceptance::ExitCode::Arguments);
        }
        const auto value = args[++i];
        if (key == "--backend")
            backend = value;
        else if (key == "--port" || key == "--bridge-port")
            port = value;
        else if (key == "--responder-port")
            responderPort = value;
        else
            report = value;
    }
    const bool bench = backend == "two-nano-bench";
    if ((backend != "native" && backend != "serial" && !bench) || report.isEmpty() ||
        (backend == "serial" && port.isEmpty()) || (backend == "native" && keys.contains("--port")) ||
        (!bench && (keys.contains("--bridge-port") || keys.contains("--responder-port"))) ||
        (bench &&
         (keys.contains("--port") || !keys.contains("--bridge-port") || !keys.contains("--responder-port") ||
          !hd::bench::distinctPorts(port.toStdString(), responderPort.toStdString())))) {
        std::cerr << "Select backend explicitly; serial requires --port; bench requires two distinct "
                     "explicit ports; native accepts no port.\n";
        usage();
        return static_cast<int>(hd::acceptance::ExitCode::Arguments);
    }
    config.backend = bench                 ? hd::acceptance::Backend::TwoNanoBench
                     : backend == "serial" ? hd::acceptance::Backend::Serial
                                           : hd::acceptance::Backend::Native;
    config.port = port.toStdString();
    config.responderPort = responderPort.toStdString();
    config.reportDirectory = std::filesystem::absolute(path(report));
    config.bridgeSettings.bootMs = bench ? 1800 : backend == "serial" ? 1500 : 0;
    QElapsedTimer elapsed;
    elapsed.start();
    const auto clock = [&] { return static_cast<hd::Time>(elapsed.elapsed()); };
    std::unique_ptr<hd::Transport> transport, responder;
    if (backend == "serial" || bench) {
#ifdef HONDADASH_WITH_SERIAL
        auto serial = std::make_unique<hd::SerialTransport>(clock);
        serial->setPortName(config.port);
        transport = std::move(serial);
        if (bench) {
            auto peer = std::make_unique<hd::SerialTransport>(clock);
            peer->setPortName(config.responderPort);
            responder = std::move(peer);
        }
#else
        std::cerr << "This build has no SerialPort. Native acceptance is available.\n";
        return static_cast<int>(hd::acceptance::ExitCode::Arguments);
#endif
    } else
        transport = std::make_unique<hd::bridge::NativeTransport>();
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::unique_ptr<hd::acceptance::Runner> runnerStorage;
    if (responder)
        runnerStorage = std::make_unique<hd::acceptance::Runner>(*transport, *responder, config);
    else
        runnerStorage = std::make_unique<hd::acceptance::Runner>(*transport, config);
    auto &runner = *runnerStorage;
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(2);
    std::string lastPhase;
    QObject::connect(&timer, &QTimer::timeout, &application, [&] {
        if (cancelled) {
            runner.cancel(clock());
            cancelled = 0;
        }
        runner.tick(clock());
        if (lastPhase != runner.phaseName()) {
            lastPhase = runner.phaseName();
            std::cout << lastPhase << '\n';
        }
        if (runner.done())
            application.exit(static_cast<int>(runner.exitCode()));
    });
    runner.start(clock());
    if (!runner.done()) {
        timer.start();
        application.exec();
    }
    std::cout << (runner.exitCode() == hd::acceptance::ExitCode::Success ? "PASS: " : "FAIL: ")
              << runner.message() << '\n'
              << "Report: " << report.toStdString() << "/report.json\n";
    return static_cast<int>(runner.exitCode());
}
