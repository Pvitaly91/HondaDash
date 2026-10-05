#include "bridge/client.hpp"
#include "../../firmware/nano_dlc_bridge_lab/wire.hpp"
#include "honda_dlc/profile.hpp"
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace hd::bridge {
namespace w = hd_bridge;
namespace {
bool equal(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}
} // namespace
Client::Client(Transport &transport, Settings settings) : transport_(transport), settings_(settings) {
    if (!settings.openMs || !settings.txMs || !settings.handshakeMs || !settings.acceptMs ||
        settings.resultMs < 500 || !settings.maxResultAgeMs || settings.maxResultAgeMs > settings.resultMs)
        throw std::invalid_argument("invalid bridge host watchdog budget");
}
Client::~Client() {
    lifetime_.reset();
    transport_.close();
}
const char *Client::stateName() const {
    switch (state_) {
    case State::Disconnected:
        return "Disconnected";
    case State::Opening:
        return "Opening";
    case State::BootWaiting:
        return "BootWaiting";
    case State::Handshaking:
        return "Handshaking";
    case State::Ready:
        return "Ready — needs new experiment";
    case State::Starting:
        return "Starting experiment";
    case State::Initializing:
        return "Initializing DLC";
    case State::Running:
        return "Running virtual DLC";
    case State::Faulted:
        return "Faulted — needs new experiment";
    }
    return "Unknown";
}
void Client::emitRaw(std::string kind, Time now, std::string detail, std::span<const std::uint8_t> bytes,
                     std::optional<BridgeTrace> trace, bool associated) {
    RawEvent event{now,
                   modelSession_ ? modelSession_ : session_,
                   associated && pending_ ? pending_->transaction : 0,
                   std::move(kind),
                   std::move(detail),
                   {bytes.begin(), bytes.end()}, std::move(trace)};
    if (onRaw)
        onRaw(event);
    if (!onRaw && callbacks_.raw)
        callbacks_.raw(event);
}
void Client::connect(Time now) {
    startRequested_ = false; // Public reconnect is strictly handshake-only.
    ++epoch_;
    const auto epoch = epoch_;
    transport_.close();
    parser_.reset();
    pending_.reset();
    info_.reset();
    error_.clear();
    diagnostics_ = {};
    eventSequence_ = 0;
    static std::atomic<std::uint32_t> ids{0x42000000};
    session_ = ++ids;
    if (!session_)
        session_ = ++ids;
    nextId_ = 0;
    now_ = now;
    state_ = State::Opening;
    openingDeadline_ = now + settings_.openMs;
    bootAt_ = now + settings_.bootMs;
    const std::weak_ptr<int> alive = lifetime_;
    TransportCallbacks callbacks;
    callbacks.opened = [this, alive, epoch](Time at) {
        if (alive.expired() || epoch != epoch_)
            return;
        state_ = State::BootWaiting;
        bootAt_ = at + settings_.bootMs;
    };
    callbacks.received = [this, alive, epoch](auto bytes, Time at) {
        if (!alive.expired() && epoch == epoch_)
            receive(bytes, at);
    };
    callbacks.sent = [this, alive, epoch](Time at) {
        if (alive.expired() || epoch != epoch_ || !pending_)
            return;
        pending_->sent = true;
        pending_->sentAt = at;
    };
    callbacks.error = [this, alive, epoch](const auto &reason, Time at) {
        if (alive.expired() || epoch != epoch_)
            return;
        info_.reset();
        fail("USB transport: " + reason, at);
        transport_.close();
    };
    callbacks.raw = [this, alive, epoch](const auto &kind, auto bytes, Time at) {
        if (!alive.expired() && epoch == epoch_)
            emitRaw(kind == "TX"   ? "usb_tx"
                    : kind == "RX" ? "usb_rx"
                                   : "usb_notice",
                    at, "Actual host transport bytes; outer CRC16/session/operation envelope", bytes);
    };
    transport_.start(std::move(callbacks), now);
}
void Client::disconnect(Time now) {
    ++epoch_;
    startRequested_ = false;
    pending_.reset();
    transport_.close();
    parser_.reset();
    info_.reset();
    state_ = State::Disconnected;
    emitRaw("bridge_state", now, "USB closed; outstanding operations cancelled, no retry");
}
void Client::start(std::uint32_t session, Time now, dlc::LinkCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    modelSession_ = session;
    startRequested_ = true;
    now_ = now;
    if (transport_.isOpen() && info_ && !pending_)
        boundary(now);
    else if (state_ != State::Opening && state_ != State::BootWaiting && state_ != State::Handshaking) {
        connect(now);
        // This path originates only in the explicit laboratory start operation.
        if (state_ != State::Faulted)
            startRequested_ = true;
    }
}
void Client::command(std::uint8_t operation, std::vector<std::uint8_t> payload, Time now,
                     std::uint32_t transaction, dlc::Read read, std::span<const std::uint8_t> bytes) {
    if (pending_ || !transport_.isOpen()) {
        fail("USB command attempted while busy or disconnected", now);
        return;
    }
    if (++nextId_ == 0) {
        fail("USB operation ID exhausted; reconnect required", now);
        return;
    }
    const auto id = nextId_;
    pending_ = Pending{operation, id, transaction, now, now, false, read, {bytes.begin(), bytes.end()}};
    const auto frame = hd::encode({operation, session_, id, std::move(payload)});
    if (!transport_.send(frame, now))
        fail("USB frame could not be queued; execution uncertain, no automatic retry", now);
}
void Client::boundary(Time now) {
    state_ = State::Starting;
    error_.clear();
    command(w::NewExperiment, {w::PolicyVersion}, now);
}
void Client::abort(Time now) {
    now_ = std::max(now_, now);
    startRequested_ = false;
    if (!transport_.isOpen() || !info_)
        return;
    // The active outer ID is retired. An old result is still parsed and logged,
    // but cannot be delivered to a newly selected read of the same DLC length.
    pending_.reset();
    state_ = State::Ready;
    command(w::Abort, {w::PolicyVersion}, now_);
}
bool Client::canExecute() const {
    return state_ == State::Running && !pending_ && !configDirty_ && info_.has_value();
}
bool Client::execute(std::span<const std::uint8_t> bytes, dlc::Read read, std::uint32_t transaction,
                     Time now) {
    const auto expected = dlc::encode(dlc::Operation::Read, read);
    if (!canExecute() || !expected || !equal(bytes, *expected))
        return false;
    std::vector<std::uint8_t> payload{w::PolicyVersion, static_cast<std::uint8_t>(read.length + 3)};
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    command(w::Execute, std::move(payload), now, transaction, read, bytes);
    return pending_.has_value();
}
void Client::setScenario(dlc::Scenario scenario) {
    scenario_ = scenario;
    configDirty_ = true;
    ++configRevision_;
}
void Client::setFaults(dlc::Faults faults) {
    faults_ = faults;
    configDirty_ = true;
    ++configRevision_;
}
void Client::configure(Time now) {
    if (scenario_ != dlc::Scenario::Baseline && scenario_ != dlc::Scenario::Higher &&
        scenario_ != dlc::Scenario::Boundary) {
        fail("Bridge-lab supports raw A/B/boundary only", now);
        return;
    }
    std::uint8_t fault = w::NoFault;
    unsigned count = 0;
    const auto select = [&](bool enabled, std::uint8_t value) {
        if (enabled) {
            fault = value;
            ++count;
        }
    };
    select(faults_.silent, w::Silent);
    select(faults_.delayMs != 0, w::Delay);
    select(faults_.gapMs != 0, w::Gap);
    select(faults_.corruptNext, w::Checksum);
    select(faults_.wrongLengthNext, w::Length);
    select(faults_.truncateNext, w::Truncated);
    select(faults_.noiseNext, w::Noise);
    select(faults_.trailingNext, w::Trailing);
    select(faults_.headerNext, w::Header);
    if (count > 1 || faults_.delayMs > 10000 || faults_.gapMs > 10000) {
        fail("Invalid bridge laboratory fault configuration", now);
        return;
    }
    std::vector<std::uint8_t> payload{
        w::PolicyVersion, static_cast<std::uint8_t>(scenario_), fault, 0, 0, 0, 0};
    w::store16(payload.data() + 3, static_cast<std::uint16_t>(faults_.delayMs));
    w::store16(payload.data() + 5, static_cast<std::uint16_t>(faults_.gapMs));
    sentConfigRevision_ = configRevision_;
    command(w::Configure, std::move(payload), now);
}
void Client::fail(std::string reason, Time now, bool timeout) {
    pending_.reset();
    startRequested_ = false;
    state_ = State::Faulted;
    error_ = std::move(reason);
    const auto context = modelSession_;
    emitRaw("bridge_fault", now, error_ + "; no automatic EXECUTE retry; new laboratory experiment required");
    if (state_ == State::Faulted && context == modelSession_ && callbacks_.fault)
        callbacks_.fault(error_, now, timeout);
}
void Client::receive(std::span<const std::uint8_t> bytes, Time now) {
    const auto epoch = epoch_;
    const auto errors = parser_.errors();
    const auto frames = parser_.feed(bytes);
    if (parser_.errors() != errors) {
        fail("USB envelope CRC/framing error; DLC context uncertain", now);
        return;
    }
    for (const auto &value : frames) {
        frame(value, now);
        if (epoch != epoch_)
            return;
    }
}
void Client::frame(const Frame &value, Time now) {
    const auto frameEpoch = epoch_;
    if (value.session != session_) {
        emitRaw("bridge_ignored", now, "Old/foreign outer session; no model update");
        return;
    }
    const auto &p = value.payload;
    if (value.type == w::Event) {
        if (!info_ || value.request != 0 || p.size() < w::EventHeader || p[0] != w::Version ||
            p.size() != std::size_t(w::EventHeader) + p[10] || p[10] > w::MaxDlcRx) {
            fail("Malformed bridge event", now);
            return;
        }
        const auto generation = w::load32(p.data() + 1);
        const bool currentGeneration = generation == info_->generation;
        const auto sequence = w::load16(p.data() + 5);
        BridgeTrace trace;
        trace.generation = generation;
        trace.sequence = sequence;
        trace.status = p[7];
        trace.rxElapsedMs = w::load16(p.data() + 8);
        emitRaw("bridge_event", now,
                currentGeneration ? "Unassociated DLC RX reported by bridge; never a measurement"
                                  : "Old generation unassociated DLC RX; never a measurement",
                std::span(p).subspan(w::EventHeader), trace, false);
        if (frameEpoch != epoch_)
            return;
        if (!currentGeneration)
            return;
        if (sequence != static_cast<std::uint16_t>(eventSequence_ + 1)) {
            fail("Bridge event sequence gap/duplicate; trace completeness lost", now);
            return;
        }
        eventSequence_ = sequence;
        if (state_ == State::Running || state_ == State::Initializing || p[7] == w::Overflow)
            fail("Unassociated DLC event / endpoint overflow", now);
        return;
    }
    if (!pending_ || value.request != pending_->id) {
        if (value.type == w::Result && p.size() >= w::ResultHeader && p[0] == w::Version && p[9] <= 11 &&
            p[10] <= w::MaxDlcRx && p.size() == std::size_t(w::ResultHeader) + p[9] + p[10]) {
            BridgeTrace trace;
            trace.generation = w::load32(p.data() + 1);
            trace.operation = value.request;
            trace.status = p[5];
            trace.txElapsedMs = w::load16(p.data() + 11);
            trace.rxElapsedMs = w::load16(p.data() + 13);
            trace.maxGapMs = w::load16(p.data() + 15);
            emitRaw("bridge_dlc_tx", now, "Retired operation result; bridge-reported actual DLC TX",
                    std::span(p).subspan(w::ResultHeader, p[9]), trace, false);
            emitRaw("bridge_dlc_rx", now,
                    "Retired operation result; DLC bytes unassociated, never a measurement",
                    std::span(p).subspan(w::ResultHeader + p[9], p[10]), trace, false);
        }
        emitRaw("bridge_ignored", now, "Old/duplicate outer operation result; no DLC retry or model update",
                {}, {}, false);
        return;
    }
    const auto operation = *pending_;
    if (operation.command == w::Hello) {
        if (value.type != w::HelloInfo || p.size() < 15 || p[0] != w::Version || p[1] != w::PolicyVersion ||
            p[2] != w::BackendVirtual || p[3] != 0 || w::load16(p.data() + 4) != w::Capabilities ||
            p[10] > w::Faulted || p[11] != 1 || p[12] != 0 || p[13] != 0 || p.size() != 15u + p[14] ||
            std::string(p.begin() + 15, p.end()) != w::Identity) {
            info_.reset();
            fail("Endpoint is not the supported virtual bridge-lab identity/version/policy (M1 rejected)",
                 now);
            return;
        }
        info_ = Info{w::Identity, p[0], p[1], p[2], false, w::load16(p.data() + 4), w::load32(p.data() + 6),
                     std::to_string(p[11]) + "." + std::to_string(p[12]) + "." + std::to_string(p[13])};
        pending_.reset();
        state_ = State::Ready;
        emitRaw("bridge_state", now,
                "Verified virtual bridge protocol identity; not hardware authentication");
        if (startRequested_ && state_ == State::Ready && !pending_ && info_)
            boundary(now);
        return;
    }
    if (value.type == w::Ack || value.type == w::Error) {
        if (p.size() != w::AckSize || p[0] != w::Version || p[5] != operation.command || !info_) {
            fail("Malformed bridge ACK/error", now);
            return;
        }
        const auto generation = w::load32(p.data() + 1);
        if (p[6] != w::Ok || value.type == w::Error) {
            if (p[6] == w::NotBound)
                info_.reset();
            fail("Bridge rejected operation; status=" + std::to_string(p[6]) +
                     "; handshake/new experiment required",
                 now);
            return;
        }
        if (operation.command != w::NewExperiment && generation != info_->generation) {
            info_.reset();
            fail("Bridge generation changed; old operation invalid", now);
            return;
        }
        pending_.reset();
        if (operation.command == w::NewExperiment) {
            info_->generation = generation;
            eventSequence_ = 0;
            configDirty_ = true;
            emitRaw("bridge_boundary", now,
                    "Explicit NEW_EXPERIMENT replaced VirtualHondaEcu and discarded old events; not physical "
                    "ECU recovery");
            if (startRequested_ && state_ == State::Starting && !pending_)
                configure(now);
        } else if (operation.command == w::Configure) {
            configDirty_ = sentConfigRevision_ != configRevision_;
            if (!configDirty_) {
                faults_.corruptNext = faults_.wrongLengthNext = faults_.truncateNext = faults_.noiseNext =
                    false;
                faults_.trailingNext = faults_.headerNext = false;
            }
            if (startRequested_) {
                state_ = State::Initializing;
                command(w::Initialize, {w::PolicyVersion}, now, 0, {}, dlc::Initialization);
            }
        } else if (operation.command == w::Abort) {
            state_ = State::Ready;
            emitRaw("bridge_state", now, "ABORT retired context; late DLC RX remains drainable");
        } else {
            fail("Unexpected ACK is not an operation result", now);
        }
        return;
    }
    if (value.type != w::Result || p.size() < w::ResultHeader || p[0] != w::Version || !info_ ||
        w::load32(p.data() + 1) != info_->generation || p[6] != operation.command ||
        p[7] != operation.read.address || p[8] != operation.read.length || p[9] > operation.dlcBytes.size() ||
        (p[5] == w::Ok && p[9] != operation.dlcBytes.size()) || p[10] > w::MaxDlcRx ||
        p.size() != std::size_t(w::ResultHeader) + p[9] + p[10] ||
        !equal(std::span(p).subspan(w::ResultHeader, p[9]), std::span(operation.dlcBytes).first(p[9]))) {
        fail("Bridge result context/length/TX bytes mismatch", now);
        return;
    }
    BridgeTrace trace;
    trace.generation = info_->generation;
    trace.operation = value.request;
    trace.status = p[5];
    trace.txElapsedMs = w::load16(p.data() + 11);
    trace.rxElapsedMs = w::load16(p.data() + 13);
    trace.maxGapMs = w::load16(p.data() + 15);
    diagnostics_ = {trace.generation,
                    trace.operation,
                    trace.status,
                    trace.txElapsedMs,
                    trace.rxElapsedMs,
                    trace.maxGapMs,
                    "DLC TX=" + std::to_string(trace.txElapsedMs) + " ms, RX=" +
                        std::to_string(trace.rxElapsedMs) + " ms, gap=" + std::to_string(trace.maxGapMs) +
                        " ms; host roundtrip=" + std::to_string(now - operation.started) +
                        " ms; command=" + std::to_string(operation.command) +
                        "; address=" + std::to_string(operation.read.address) +
                        "; length=" + std::to_string(operation.read.length)};
    const auto raw = std::span(p).subspan(w::ResultHeader + p[9], p[10]);
    const auto epoch = epoch_;
    emitRaw("bridge_dlc_tx", now, "Bridge-reported actual DLC TX; " + diagnostics_.summary,
            std::span(p).subspan(w::ResultHeader, p[9]), trace);
    emitRaw("bridge_dlc_rx", now, "Bridge-reported DLC RX including malformed/incomplete bytes", raw, trace);
    emitRaw("bridge_result", now, diagnostics_.summary, {}, trace);
    if (epoch != epoch_ || !pending_ || pending_->id != operation.id)
        return;
    pending_.reset();
    if (p[5] != w::Ok) {
        fail("DLC engine failed status=" + std::to_string(p[5]), now,
             p[5] == w::DlcTotalTimeout || p[5] == w::DlcInterbyteTimeout || p[5] == w::DlcTxTimeout);
        return;
    }
    if (trace.txElapsedMs >= w::DlcTxTimeoutMs ||
        (operation.command == w::Execute &&
         (trace.rxElapsedMs < w::TotalTimeoutMs || trace.maxGapMs >= w::InterbyteTimeoutMs))) {
        fail("Bridge OK result contradicts embedded timing policy", now);
        return;
    }
    if (operation.command == w::Initialize) {
        if (!raw.empty() || trace.rxElapsedMs < w::InitWaitMs) {
            fail("Invalid initialization result facts", now);
            return;
        }
        state_ = State::Running;
        startRequested_ = false;
        if (callbacks_.ready)
            callbacks_.ready(now);
    } else if (operation.command == w::Execute) {
        if (now < operation.started || now - operation.started > settings_.maxResultAgeMs) {
            fail("Result exceeds conservative host age budget; no renewed freshness", now, true);
            return;
        }
        if (raw.size() != std::size_t(operation.read.length) + 3) {
            fail("DLC successful result has wrong raw length", now);
            return;
        }
        dlc::Parser validation;
        validation.expect(operation.read.length);
        auto check = dlc::Parser::Result::Incomplete;
        for (auto byte : raw) {
            check = validation.feed(byte);
            if (check != dlc::Parser::Result::Incomplete && check != dlc::Parser::Result::Complete)
                break;
        }
        if (check != dlc::Parser::Result::Complete) {
            fail("Bridge OK result failed independent host DLC header/length/checksum validation", now);
            return;
        }
        // No device timestamp is compared with the host clock. The request start
        // is a conservative lower bound, so USB delay consumes existing freshness.
        if (callbacks_.received)
            callbacks_.received(raw, now, operation.started);
    } else {
        fail("Unexpected terminal result operation", now);
    }
}
void Client::tick(Time now) {
    now_ = now;
    const auto epoch = epoch_;
    if (state_ == State::Opening && now >= openingDeadline_)
        fail("USB open watchdog: transport did not report opened", now, true);
    if (state_ == State::BootWaiting && now >= bootAt_) {
        state_ = State::Handshaking;
        command(w::Hello, {w::Version, w::PolicyVersion}, now);
    }
    if (pending_) {
        const auto &p = *pending_;
        const auto budget = !p.sent                                                   ? settings_.txMs
                            : p.command == w::Hello                                   ? settings_.handshakeMs
                            : (p.command == w::Initialize || p.command == w::Execute) ? settings_.resultMs
                                                                                      : settings_.acceptMs;
        const auto beginning = p.sent ? p.sentAt : p.started;
        if (now >= beginning && now - beginning >= budget)
            fail(!p.sent ? "USB transmit watchdog" : "Bridge result/acceptance watchdog; execution uncertain",
                 now, true);
    }
    transport_.tick(now); // Faulted clients still drain and record USB/unassociated DLC events.
    if (epoch != epoch_)
        return;
    if (state_ == State::Running && !pending_ && configDirty_)
        configure(now);
}
} // namespace hd::bridge
