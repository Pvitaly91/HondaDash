#pragma once

#include "bench/controller.hpp"
#include "bridge/client.hpp"
#include "honda_dlc/session.hpp"
#include "recording/recorder.hpp"
#include <chrono>
#include <filesystem>
#include <memory>

namespace hd::acceptance {
enum class Backend { Native, Serial, TwoNanoBench };
enum class ExitCode {
    Success = 0,
    Arguments = 2,
    Endpoint = 3,
    Timeout = 4,
    Assertion = 5,
    Recording = 6,
    Cancelled = 7,
    Report = 8
};
struct Config {
    Backend backend{Backend::Native};
    std::string port, desktopVersion{"unknown"}, desktopSha{"unknown"}, os{"unknown"};
    std::string responderPort;
    std::filesystem::path reportDirectory;
    Time observationMs{10000}, phaseTimeoutMs{8000}, overallTimeoutMs{45000};
    bridge::Settings bridgeSettings{};
};
// One production state machine shared by the CLI, native tests and Linux PTY.
// All deadlines use the caller's monotonic host time. Responses are asynchronous;
// finalization closes the port and joins the Recorder worker to verify its flush.
class Runner {
  public:
    Runner(Transport &, Config, Recorder::WriteHook writeHook = {});
    Runner(Transport &bridge, Transport &responder, Config, Recorder::WriteHook writeHook = {});
    ~Runner();
    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;
    void start(Time now);
    void tick(Time now);
    void cancel(Time now);
    bool done() const;
    ExitCode exitCode() const;
    const std::string &message() const;
    const char *phaseName() const;
    std::filesystem::path reportPath() const;
    std::filesystem::path journalDirectory() const;
    const dlc::Session &session() const;
    const bridge::Client &client() const;
    const bench::Controller *benchController() const;
    // Harness-only bounded worker drain; never part of state-machine deadlines.
    bool flushRecording(std::chrono::milliseconds timeout);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hd::acceptance
