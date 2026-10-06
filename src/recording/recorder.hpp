#pragma once

#include "application/session.hpp"
#include "core/model.hpp"
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace hd {

struct RecordingMetadata {
    std::string scenario;
    std::uint32_t seed{};
    std::string transport{"in-memory"}, endpoint{"desktop-emulator"}, firmware{"0.2.0"}, port;
    std::uint32_t baud{};
    // Version 2 preserves the M1 snapshot format. Version 3 explicitly records
    // partial updates, provenance and host-only transaction metadata.
    std::string wireProtocol{"synthetic-demo-v1"}, profile{"synthetic-demo-v1"};
    std::uint32_t profileVersion{1};
    std::string evidenceStatus{"synthetic"}, fixtureClass{"synthetic"}, fixtureId;
    bool hardwareVerified{}, liveEnabled{};
    std::uint32_t formatVersion{2};
    FreshnessSettings freshness{};
    // Additive v3 metadata. Inner DLC bytes/partial measurement semantics stay unchanged.
    std::string bridgeIdentity, backend, outerProtocol;
    std::uint32_t bridgeVersion{}, readPolicyVersion{};
    bool physicalDlcEnabled{};
    std::string schedulerPolicy, freshnessPolicy, timingEstimateSource, measurementScope;
    std::uint32_t schedulerPolicyVersion{}, freshnessPolicyVersion{};
    std::array<Time, ChannelCount> requestedIntervals{};
    // M3a bench semantics are distinct from the legacy virtual DLC-disabled field.
    std::string responderIdentity, responderFirmware, responderPort, firmwareTarget;
    std::uint32_t responderProtocolVersion{}, responderReadPolicyVersion{}, benchSchemaVersion{};
    bool benchIoEnabled{}, vehicleConnectionAllowed{};
};

// Disk work runs on one bounded worker. A failed enqueue stops recording and
// preserves an error for the dashboard; callers must never silently ignore it.
class Recorder {
public:
    // Optional hook is a deterministic test seam: false simulates a disk failure.
    // It runs on the writer thread, immediately before each queued record.
    using WriteHook = std::function<bool()>;
    explicit Recorder(std::size_t capacity = 256, WriteHook beforeWrite = {});
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Returns whether asynchronous start was accepted. The UI polls status/error
    // to observe directory/open errors without blocking its event loop.
    bool start(std::filesystem::path directory, RecordingMetadata metadata);
    void stop(); // drains accepted records, flushes both files, and joins
    // Bounded test/console synchronization only; never call from a GUI tick.
    bool waitUntilIdle(std::chrono::milliseconds timeout);
    bool enqueueRaw(const RawEvent& event);
    bool enqueueSample(const Sample& sample);
    std::string status() const;
    std::string error() const;
    bool active() const;
    std::filesystem::path directory() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hd
