#include "application/session.hpp"
#include <atomic>
#include <limits>
#include <random>
#include <stdexcept>
namespace hd {
namespace {
std::uint32_t nextIdentity() {
    static std::atomic<std::uint64_t> counter{[] {
        std::random_device r;
        return (std::uint64_t(r()) << 32) | r();
    }()};
    auto v = static_cast<std::uint32_t>(++counter);
    if (!v)
        v = static_cast<std::uint32_t>(++counter);
    return v;
}
void validate(SessionSettings s) {
    if (!s.pollMs || !s.timeoutMs || !s.helloAttempts || !s.txTimeoutMs ||
        s.handshakeDeadlineMs <= s.bootDelayMs)
        throw std::invalid_argument("Invalid session settings");
}
} // namespace
Session::Session(Transport &t, SessionSettings s, FreshnessSettings f, IdGenerator ids)
    : transport_(&t), settings_(s), model_(f), ids_(ids ? std::move(ids) : nextIdentity) {
    validate(s);
}
Session::~Session() {
    lifetime_.reset();
    ++generation_;
    transport_->close();
}
void Session::setTransport(Transport &t) {
    if (state_ != SessionState::Stopped)
        throw std::logic_error("Stop before changing transport");
    transport_ = &t;
}
void Session::setSettings(SessionSettings s) {
    if (state_ != SessionState::Stopped)
        throw std::logic_error("Stop before settings");
    validate(s);
    settings_ = s;
}
void Session::event(std::string k, Time n, std::uint32_t r, std::string d, std::vector<std::uint8_t> b) {
    if (onRaw)
        onRaw({n, sessionId_, r, std::move(k), std::move(d), std::move(b)});
}
void Session::fail(std::string message, Time n) {
    error_ = std::move(message);
    state_ = SessionState::Faulted;
    ++generation_;
    pending_.reset();
    parser_.reset();
    transport_->close();
    event("error", n, 0, error_);
}
bool Session::identity(Time n) {
    auto id = ids_();
    if (!id || id == sessionId_) {
        fail("Session identity repeated or zero", n);
        return false;
    }
    sessionId_ = id;
    nextRequest_ = 0;
    helloSent_ = 0;
    helloOk_ = false;
    controlLast_ = false;
    info_.reset();
    pending_.reset();
    parser_.reset();
    model_.reset();
    acceptedTimes_.clear();
    for (auto &c : controls_)
        c.dirty = !c.payload.empty();
    return true;
}
void Session::start(Time n) {
    const auto afterStop = generation_ + 1;
    stop(n);
    if (generation_ != afterStop)
        return;
    error_.clear();
    stats_ = {};
    if (!identity(n))
        return;
    state_ = SessionState::Opening;
    handshakeUntil_ = n + settings_.handshakeDeadlineMs;
    auto generation = generation_;
    std::weak_ptr<int> alive = lifetime_;
    auto valid = [this, alive, generation] { return !alive.expired() && generation == generation_; };
    TransportCallbacks cb;
    cb.opened = [this, valid](Time t) {
        if (valid())
            opened(t);
    };
    cb.received = [this, valid](auto b, Time t) {
        if (valid())
            receive(b, t);
    };
    cb.sent = [this, valid](Time t) {
        if (valid())
            transmitted(t);
    };
    cb.error = [this, valid](const auto &e, Time t) {
        if (valid())
            fail(e, t);
    };
    cb.raw = [this, valid](const auto &k, auto b, Time t) {
        if (valid())
            event(k, t, pending_ ? pending_->request : 0, {}, {b.begin(), b.end()});
    };
    event("start", n, 0, transport_->name());
    if (valid())
        transport_->start(std::move(cb), n);
}
void Session::stop(Time n) {
    ++generation_;
    transport_->close();
    const bool wasActive = state_ != SessionState::Stopped;
    state_ = SessionState::Stopped;
    pending_.reset();
    info_.reset();
    parser_.reset();
    acceptedTimes_.clear();
    model_.reset();
    stats_.responseHz = 0;
    if (wasActive)
        event("stop", n);
}
void Session::opened(Time n) {
    if (state_ == SessionState::Opening) {
        state_ = SessionState::BootWaiting;
        bootUntil_ = n + settings_.bootDelayMs;
        event("boot_wait", n);
    }
}
void Session::handshake(Time n) {
    state_ = SessionState::Handshaking;
    send(helloOk_ ? GetDeviceInfo : Hello, n);
}
void Session::send(std::uint8_t type, Time n, std::vector<std::uint8_t> payload, std::uint64_t revision) {
    if (pending_ || transport_->pendingBytes()) {
        fail("Previous TX still pending", n);
        return;
    }
    if (nextRequest_ == std::numeric_limits<std::uint32_t>::max()) {
        fail("Request IDs exhausted; reconnect", n);
        return;
    }
    auto request = ++nextRequest_;
    pending_ = Pending{request, type, n + settings_.txTimeoutMs, false, revision};
    if (type == Hello)
        ++helloSent_;
    if (type == ReadSnapshot)
        nextPoll_ = n + settings_.pollMs;
    auto bytes = encode({type, sessionId_, request, std::move(payload)});
    const auto generation = generation_;
    event("TX_QUEUED", n, request, {}, bytes);
    if (generation != generation_)
        return;
    if (!transport_->send(bytes, n) && generation == generation_ && state_ != SessionState::Faulted)
        fail("Transport rejected request", n);
}
void Session::transmitted(Time n) {
    if (pending_ && !pending_->sent) {
        if (n >= pending_->deadline) {
            fail("TX deadline exceeded", n);
            return;
        }
        pending_->sent = true;
        pending_->deadline = n + settings_.timeoutMs;
        event("TX_DRAINED", n, pending_->request);
    }
}
void Session::receive(std::span<const std::uint8_t> bytes, Time n) {
    if (state_ == SessionState::Stopped || state_ == SessionState::Faulted)
        return;
    if (state_ != SessionState::Running && n >= handshakeUntil_) {
        fail("Handshake deadline exceeded", n);
        return;
    }
    const auto generation = generation_;
    auto before = parser_.errors();
    auto frames = parser_.feed(bytes);
    stats_.corrupt += parser_.errors() - before;
    for (const auto &f : frames) {
        if (generation != generation_)
            return;
        if (!pending_ || !pending_->sent || f.session != sessionId_ || f.request != pending_->request ||
            n >= pending_->deadline) {
            ++stats_.ignored;
            event("ignored", n, f.request, "Unmatched, duplicate or expired response");
            continue;
        }
        auto type = pending_->type;
        if (f.type == ErrorResponse) {
            if (f.payload == std::vector<std::uint8_t>{3}) {
                event("endpoint_reset", n, f.request);
                if (generation != generation_)
                    return;
                if (identity(n)) {
                    handshakeUntil_ = n + settings_.handshakeDeadlineMs;
                    state_ = SessionState::Handshaking;
                }
            } else if (f.payload == std::vector<std::uint8_t>{4} && type == ReadSnapshot) {
                pending_.reset();
                ++stats_.ignored;
                event("snapshot_busy", n, f.request);
            } else
                fail("Endpoint rejected command: " +
                         (f.payload.empty() ? std::string("empty error") : std::to_string(f.payload[0])),
                     n);
        } else if (type == Hello && f.type == HelloResponse) {
            const std::string profile = "synthetic-demo-v1";
            std::vector<std::uint8_t> expected{1};
            expected.insert(expected.end(), profile.begin(), profile.end());
            if (f.payload != expected) {
                fail("Incompatible synthetic profile", n);
                continue;
            }
            pending_.reset();
            helloOk_ = true;
        } else if (type == GetDeviceInfo && f.type == (GetDeviceInfo | 0x80)) {
            info_ = decodeDeviceInfo(f.payload);
            if (!info_) {
                fail("Incompatible synthetic endpoint", n);
                continue;
            }
            pending_.reset();
            state_ = SessionState::Running;
            nextPoll_ = n;
            event("ready", n, f.request, info_->endpoint + " " + info_->firmware);
        } else if (type == ReadSnapshot && f.type == SnapshotResponse) {
            auto sample = decodeSnapshot(f, n);
            if (!sample) {
                ++stats_.corrupt;
                event("invalid_snapshot", n, f.request);
                continue;
            }
            pending_.reset();
            model_.apply(*sample);
            ++stats_.accepted;
            acceptedTimes_.push_back(n);
            if (onSample)
                onSample(*sample);
        } else if (type >= SetScenario && type <= SetQuality && f.type == (type | 0x80) &&
                   f.payload == std::vector<std::uint8_t>{0}) {
            auto &c = controls_[type - SetScenario];
            if (c.revision == pending_->revision) {
                c.dirty = false;
                if (type == SetFaults)
                    c.payload[3] = 0;
            }
            pending_.reset();
            event("control_ack", n, f.request);
        } else {
            ++stats_.ignored;
            event("unexpected_response", n, f.request);
        }
    }
}
void Session::desired(std::size_t i, std::vector<std::uint8_t> p) {
    auto &c = controls_.at(i);
    c.payload = std::move(p);
    ++c.revision;
    c.dirty = true;
}
void Session::setScenario(std::uint8_t s, std::uint32_t seed) {
    desired(0, scenarioPayload(s, seed));
}
void Session::setManual(const std::array<double, ChannelCount> &v) {
    desired(1, manualPayload(v));
}
void Session::setFaults(DeviceFaults f) {
    desired(2, faultsPayload(f));
}
void Session::setQualities(const std::array<Quality, ChannelCount> &q) {
    std::vector<std::uint8_t> p;
    for (auto v : q) {
        if (v != Quality::Valid && v != Quality::Unsupported && v != Quality::Invalid)
            throw std::invalid_argument("Wire quality");
        p.push_back(static_cast<std::uint8_t>(v));
    }
    desired(3, std::move(p));
}
void Session::tick(Time n) {
    model_.refresh(n);
    while (!acceptedTimes_.empty() && n - acceptedTimes_.front() >= 1000)
        acceptedTimes_.pop_front();
    stats_.responseHz = static_cast<double>(acceptedTimes_.size());
    if (state_ == SessionState::Stopped || state_ == SessionState::Faulted)
        return;
    const auto beforeTick = generation_;
    transport_->tick(n);
    if (generation_ != beforeTick)
        return;
    if (state_ == SessionState::Stopped || state_ == SessionState::Faulted)
        return;
    if (state_ != SessionState::Running && n >= handshakeUntil_) {
        fail("Handshake deadline exceeded", n);
        return;
    }
    if (pending_ && n >= pending_->deadline) {
        auto p = *pending_;
        const auto generation = generation_;
        ++stats_.timeouts;
        pending_.reset();
        parser_.reset();
        event("timeout", n, p.request);
        if (generation != generation_)
            return;
        if (!p.sent) {
            fail("TX deadline exceeded", n);
            return;
        }
        if (p.type != Hello && p.type != ReadSnapshot) {
            fail("Command response timeout; state uncertain", n);
            return;
        }
        if (p.type == Hello && helloSent_ >= settings_.helloAttempts) {
            fail("HELLO attempts exhausted", n);
            return;
        }
    }
    if (pending_)
        return;
    if (state_ == SessionState::BootWaiting && n >= bootUntil_)
        handshake(n);
    else if (state_ == SessionState::Handshaking)
        handshake(n);
    else if (state_ == SessionState::Running) {
        if (!controlLast_ || n < nextPoll_)
            for (std::size_t i = 0; i < controls_.size(); ++i) {
                auto &c = controls_[i];
                if (c.dirty && (info_->capabilities & (1u << i))) {
                    controlLast_ = true;
                    send(static_cast<std::uint8_t>(SetScenario + i), n, c.payload, c.revision);
                    return;
                }
            }
        if (n >= nextPoll_) {
            controlLast_ = false;
            send(ReadSnapshot, n);
        }
    }
}
} // namespace hd
