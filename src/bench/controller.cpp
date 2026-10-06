#include "bench/controller.hpp"
#include "../../firmware/shared/bench_wire.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>

namespace hd::bench {
namespace b = hd_bench;
namespace w = hd_bridge;
bool distinctPorts(std::string_view first, std::string_view second) {
    if (first.empty() || second.empty())
        return false;
    std::string a(first), c(second);
#ifdef _WIN32
    const auto normalize = [](std::string &value) {
        if (value.starts_with("\\\\.\\"))
            value.erase(0, 4);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char x) { return static_cast<char>(std::toupper(x)); });
    };
    normalize(a);
    normalize(c);
    // Windows filesystem::equivalent may open device handles. Port validation
    // must remain lexical until the user's explicit Start/check operation.
    return a != c;
#else
    if (a == c)
        return false;
    std::error_code error;
    const auto path = [](std::string_view text) {
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
    };
    return !std::filesystem::equivalent(path(first), path(second), error);
#endif
}
namespace {
bridge::Settings benchSettings(bridge::Settings settings) {
    settings.backend = bridge::Backend::TwoNanoBench;
    return settings;
}
} // namespace
Controller::Controller(Transport &bridgeTransport, Transport &responder, bridge::Settings settings)
    : bridge_(bridgeTransport, benchSettings(settings)), responder_(responder), settings_(settings) {
    bridge_.onRaw = [this](const RawEvent &event) { forward(event); };
    bridge_.onBenchBoundaryReady = [this](Time now) {
        if (state_ != State::Preparing || !responderInfo_ || !quiescent_) {
            fail("Bench boundary completed without acknowledged quiescent responder", now);
            return;
        }
        state_ = State::Arming;
        phaseAt_ = now;
        std::vector<std::uint8_t> payload{b::PolicyVersion, 0, 0, 0, 0};
        w::store32(payload.data() + 1, responderInfo_->generation);
        command(b::Arm, std::move(payload), now);
    };
}
Controller::~Controller() {
    lifetime_.reset();
    bridge_.onRaw = {};
    bridge_.onBenchBoundaryReady = {};
    responder_.close();
}
const char *Controller::stateName() const {
    switch (state_) {
    case State::Disconnected:
        return "Disconnected — two boards required";
    case State::Handshaking:
        return "Verifying both identities";
    case State::Ready:
        return "Ready — explicit new bench experiment required";
    case State::Quiescing:
        return "Waiting for responder physical TX completion and quiescence";
    case State::Draining:
        return "Draining bridge RX and checking idle bus";
    case State::Preparing:
        return "Awaiting bridge NEW boundary";
    case State::Arming:
        return "Arming acknowledged responder generation";
    case State::Running:
        return "Two-Nano bench running";
    case State::Faulted:
        return "Bench fault — explicit new experiment required";
    }
    return "Unknown";
}
void Controller::forward(const RawEvent &event) {
    if (onRaw)
        onRaw(event);
    else if (callbacks_.raw)
        callbacks_.raw(event);
}
void Controller::raw(std::string kind, Time now, std::string detail, std::span<const std::uint8_t> bytes) {
    forward({now,
             modelSession_ ? modelSession_ : usbSession_,
             0,
             std::move(kind),
             std::move(detail),
             {bytes.begin(), bytes.end()},
             {}});
}
void Controller::connect(Time now) {
    ++epoch_;
    const auto epoch = epoch_;
    responder_.close();
    parser_.reset();
    pending_.reset();
    responderInfo_.reset();
    responderDiagnostics_.clear();
    callbacks_ = {};
    opened_ = helloSent_ = startRequested_ = stopping_ = quiescent_ = false;
    collectDiagnostics_ = false;
    error_.clear();
    state_ = State::Handshaking;
    now_ = phaseAt_ = now;
    openDeadline_ = now + settings_.openMs;
    static std::atomic<std::uint32_t> sessions{0x62000000};
    usbSession_ = ++sessions;
    if (!usbSession_)
        usbSession_ = ++sessions;
    nextId_ = 0;
    const std::weak_ptr<int> alive = lifetime_;
    TransportCallbacks callbacks;
    callbacks.opened = [this, alive, epoch](Time at) {
        if (alive.expired() || epoch != epoch_)
            return;
        opened_ = true;
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
        responderInfo_.reset();
        fail("Responder USB: " + reason, at);
        responder_.close();
    };
    callbacks.raw = [this, alive, epoch](const auto &kind, auto bytes, Time at) {
        if (!alive.expired() && epoch == epoch_)
            raw(kind == "TX"   ? "responder_usb_tx"
                : kind == "RX" ? "responder_usb_rx"
                               : "responder_usb_notice",
                at, "Actual responder USB control bytes; never a measurement path", bytes);
    };
    bridge_.connect(now);
    responder_.start(std::move(callbacks), now);
}
void Controller::disconnect(Time now) {
    ++epoch_;
    startRequested_ = stopping_ = false;
    pending_.reset();
    responder_.close();
    bridge_.disconnect(now);
    responderInfo_.reset();
    parser_.reset();
    state_ = State::Disconnected;
    raw("bench_state", now, "Both explicit USB transports closed; no automatic retry/fallback");
}
void Controller::command(std::uint8_t cmd, std::vector<std::uint8_t> payload, Time now) {
    if (pending_ || !responder_.isOpen() || ++nextId_ == 0) {
        fail("Responder operation busy/disconnected/ID exhausted; execution uncertain", now);
        return;
    }
    pending_ = Pending{cmd, nextId_, now, now};
    if (!responder_.send(encode({cmd, usbSession_, nextId_, std::move(payload)}), now))
        fail("Responder control could not be queued; no automatic retry", now);
}
void Controller::receive(std::span<const std::uint8_t> bytes, Time now) {
    const auto before = parser_.errors();
    const auto values = parser_.feed(bytes);
    if (before != parser_.errors()) {
        fail("Responder USB CRC/framing error", now);
        return;
    }
    const auto epoch = epoch_;
    for (const auto &value : values) {
        frame(value, now);
        if (epoch != epoch_)
            return;
    }
}
void Controller::frame(const Frame &value, Time now) {
    if (value.session != usbSession_ || !pending_ || value.request != pending_->id) {
        raw("responder_ignored", now, "Retired/foreign control reply; never a measurement");
        return;
    }
    const auto operation = pending_->command;
    const auto &p = value.payload;
    if (operation == w::Diagnostics) {
        if (value.type != w::DiagnosticInfo || p.size() != 60 || p[0] != b::Version || !responderInfo_ ||
            w::load32(p.data() + 1) != responderInfo_->generation) {
            fail("Responder diagnostic context/length mismatch", now);
            return;
        }
        responderDiagnostics_ = p;
        pending_.reset();
        raw("responder_diagnostics", now,
            "MCU-reported responder/physical driver counters; not external analyzer facts", p);
        return;
    }
    if (operation == w::Hello) {
        if (value.type != w::HelloInfo || p.size() < 15 || p[0] != b::Version || p[1] != b::PolicyVersion ||
            p[2] != b::BackendResponder || p[3] != 1 || w::load16(p.data() + 4) != b::ResponderCapabilities ||
            p[11] != 1 || p[12] != 0 || p[13] != 0 || p.size() != 15u + p[14] ||
            std::string(p.begin() + 15, p.end()) != b::ResponderIdentity) {
            fail("Responder strict identity/version/policy/backend mismatch", now);
            return;
        }
        responderInfo_ =
            bridge::Info{b::ResponderIdentity,    p[0],    p[1], p[2], false, w::load16(p.data() + 4),
                         w::load32(p.data() + 6), "1.0.0", true};
        pending_.reset();
        raw("responder_identity", now,
            "Exact external test responder verified; identity is a firmware claim, not electrical "
            "authentication");
        return;
    }
    if ((value.type != w::Ack && value.type != w::Error) || p.size() != w::AckSize || p[0] != b::Version ||
        p[5] != operation || !responderInfo_ || p[6] != w::Ok || value.type == w::Error) {
        fail("Responder control rejected or malformed ACK; explicit recovery required", now);
        return;
    }
    const auto generation = w::load32(p.data() + 1);
    if (operation == b::Quiesce) {
        if (!generation || generation <= responderInfo_->generation) {
            fail("Responder QUIESCE did not acknowledge a new generation", now);
            return;
        }
        responderInfo_->generation = generation;
        quiescent_ = true;
        pending_.reset();
        raw("bench_boundary", now,
            "Responder QUIESCE ACK: physical TX complete, deferred replies cancelled; generation=" +
                std::to_string(generation));
        phaseAt_ = now;
        state_ = startRequested_ ? State::Draining : State::Ready;
        stopping_ = false;
        if (!startRequested_)
            collectDiagnostics_ = true;
    } else {
        if (generation != responderInfo_->generation) {
            responderInfo_.reset();
            fail("Responder generation changed; repeated identity/quiescence required", now);
            return;
        }
        pending_.reset();
        if (operation == b::Arm) {
            quiescent_ = false;
            raw("bench_boundary", now,
                "External responder armed exact acknowledged generation; no spontaneous line TX");
            configure(now);
        } else if (operation == w::Configure) {
            configDirty_ = sentConfigRevision_ != configRevision_;
            if (!configDirty_) {
                faults_.corruptNext = faults_.wrongLengthNext = faults_.truncateNext = faults_.noiseNext =
                    false;
                faults_.trailingNext = faults_.headerNext = false;
            }
            if (state_ == State::Arming && !configDirty_)
                activate(now);
        } else
            fail("Unexpected responder ACK", now);
    }
}
void Controller::fail(std::string reason, Time now, bool timeout) {
    if (state_ == State::Faulted)
        return;
    state_ = State::Faulted;
    error_ = std::move(reason);
    startRequested_ = false;
    pending_.reset();
    raw("bench_fault", now, error_ + "; no virtual fallback or EXECUTE retry");
    bridge_.abort(now);
    collectDiagnostics_ = false;
    if (callbacks_.fault)
        callbacks_.fault(error_, now, timeout);
}
void Controller::start(std::uint32_t session, Time now, dlc::LinkCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    modelSession_ = session;
    if (!bridge_.info() || !responderInfo_ || !responder_.isOpen() || state_ == State::Disconnected ||
        state_ == State::Handshaking) {
        // A fresh Session start must receive a terminal failure even when the
        // controller was already faulted by a previous handshake/USB failure.
        if (state_ == State::Faulted && callbacks_.fault) {
            callbacks_.fault(error_, now, false);
            return;
        }
        fail("Both explicit bench identities must be verified before a laboratory experiment", now);
        return;
    }
    error_.clear();
    startRequested_ = true;
    beginQuiesce(now);
}
void Controller::beginQuiesce(Time now) {
    bridge_.abort(now);
    collectDiagnostics_ = false;
    // Retire an uncertain older control operation, but never retry EXECUTE.
    pending_.reset();
    state_ = State::Quiescing;
    quiescent_ = false;
    phaseAt_ = now;
    command(b::Quiesce, {b::PolicyVersion}, now);
}
void Controller::abort(Time now) {
    startRequested_ = false;
    if (!bridge_.info() || !responderInfo_) {
        bridge_.abort(now);
        return;
    }
    stopping_ = true;
    beginQuiesce(now);
}
void Controller::setScenario(dlc::Scenario value) {
    scenario_ = value;
    configDirty_ = true;
    ++configRevision_;
}
void Controller::setFaults(dlc::Faults value) {
    faults_ = value;
    configDirty_ = true;
    ++configRevision_;
}
void Controller::configure(Time now) {
    if (scenario_ != dlc::Scenario::Baseline && scenario_ != dlc::Scenario::Higher &&
        scenario_ != dlc::Scenario::Boundary) {
        fail("Bench responder accepts raw A/B/Boundary only", now);
        return;
    }
    std::uint8_t fault = w::NoFault;
    unsigned count = 0;
    const auto select = [&](bool enabled, std::uint8_t selected) {
        if (enabled) {
            fault = selected;
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
        fail("Invalid bench responder fault configuration", now);
        return;
    }
    std::vector<std::uint8_t> payload{
        b::PolicyVersion, static_cast<std::uint8_t>(scenario_), fault, 0, 0, 0, 0};
    w::store16(payload.data() + 3, static_cast<std::uint16_t>(faults_.delayMs));
    w::store16(payload.data() + 5, static_cast<std::uint16_t>(faults_.gapMs));
    sentConfigRevision_ = configRevision_;
    command(w::Configure, std::move(payload), now);
}
void Controller::activate(Time now) {
    if (!bridge_.activateBench(now)) {
        fail("Bridge INIT attempted outside acknowledged boundary", now);
        return;
    }
}
void Controller::tick(Time now) {
    now_ = now;
    bridge_.tick(now);
    responder_.tick(now);
    if (state_ == State::Disconnected)
        return;
    if (bridge_.state() == bridge::State::Faulted && state_ != State::Faulted) {
        fail("Bridge: " + bridge_.error(), now, bridge_.error().find("watchdog") != std::string::npos);
        return;
    }
    if (state_ == State::Handshaking) {
        if (!opened_ && now >= openDeadline_) {
            fail("Responder USB open watchdog", now, true);
            return;
        }
        if (opened_ && !helloSent_ && now >= bootAt_) {
            helloSent_ = true;
            command(w::Hello, {b::Version, b::PolicyVersion}, now);
        }
        if (responderInfo_ && bridge_.info() && bridge_.state() == bridge::State::Ready) {
            state_ = State::Ready;
            raw("bench_state", now, "Both strict identities verified; no line operation performed");
        }
    }
    if (pending_) {
        const auto &p = *pending_;
        const auto limit = !p.sent                 ? settings_.txMs
                           : p.command == w::Hello ? settings_.handshakeMs
                                                   : settings_.acceptMs;
        if (now >= (p.sent ? p.sentAt : p.started) && now - (p.sent ? p.sentAt : p.started) >= limit) {
            fail("Responder USB control watchdog; execution uncertain", now, true);
            return;
        }
    }
    if (state_ == State::Draining && now - phaseAt_ >= 20 && !bridge_.operationPending()) {
        state_ = State::Preparing;
        phaseAt_ = now;
        dlc::LinkCallbacks callbacks;
        callbacks.ready = [this](Time at) {
            state_ = State::Running;
            startRequested_ = false;
            if (callbacks_.ready)
                callbacks_.ready(at);
        };
        callbacks.received = [this](auto bytes, Time at, Time since) {
            if (state_ == State::Running && callbacks_.received)
                callbacks_.received(bytes, at, since);
        };
        callbacks.fault = [this](auto reason, Time at, bool timeout) {
            fail(std::move(reason), at, timeout);
        };
        bridge_.prepareBench(modelSession_, responderInfo_->generation, now, std::move(callbacks));
    }
    if (state_ == State::Running && !pending_ && configDirty_ && bridge_.canExecute())
        configure(now);
    if (state_ == State::Arming && !pending_ && configDirty_)
        configure(now);
    if (state_ == State::Ready && collectDiagnostics_ && !pending_ && !bridge_.operationPending()) {
        collectDiagnostics_ = false;
        if (!bridge_.requestDiagnostics(now)) {
            fail("Bridge diagnostic collection blocked at Stop", now);
            return;
        }
        command(w::Diagnostics, {b::PolicyVersion}, now);
    }
}
bool Controller::canExecute() const {
    return state_ == State::Running && !pending_ && !configDirty_ && bridge_.canExecute();
}
bool Controller::execute(std::span<const std::uint8_t> bytes, dlc::Read read, std::uint32_t id, Time now) {
    return canExecute() && bridge_.execute(bytes, read, id, now);
}
std::size_t Controller::pendingBytes() const {
    return bridge_.pendingBytes() + responder_.pendingBytes() + parser_.buffered();
}
} // namespace hd::bench
