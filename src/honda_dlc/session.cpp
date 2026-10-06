#include "honda_dlc/session.hpp"
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace hd::dlc {
Session::Session(Settings settings, FreshnessSettings freshness) : settings_(settings), model_(freshness) {
    if (!settings.totalMs || !settings.interbyteMs || settings.fastMs < 20 || !settings.coolantMs)
        throw std::invalid_argument("invalid offline DLC timing policy");
    if (settings.bridgePolling &&
        (settings.bridgePolling->slotMs != BridgeSlotMs ||
         std::any_of(settings.bridgePolling->requestedIntervalsMs.begin(),
                     settings.bridgePolling->requestedIntervalsMs.end(), [](Time value) { return !value; })))
        throw std::invalid_argument("invalid bridge polling policy");
}
Session::Session(Link &link, Settings settings, FreshnessSettings freshness) : Session(settings, freshness) {
    link_ = &link;
    lab_ = dynamic_cast<LabControl *>(&link);
}
Session::~Session() {
    lifetime_.reset();
    link_->abort(0);
}
void Session::event(std::string kind, Time now, std::uint32_t transaction, std::string detail,
                    std::span<const std::uint8_t> bytes) {
    if (onRaw)
        onRaw({now,
               session_,
               transaction,
               std::move(kind),
               std::move(detail),
               {bytes.begin(), bytes.end()},
               {}});
}
void Session::start(Time now) {
    if (!profileSelected_)
        return;
    ++generation_;
    const auto generation = generation_;
    static std::atomic<std::uint32_t> ids{0};
    session_ = ++ids;
    if (!session_)
        session_ = ++ids;
    transaction_ = 0;
    state_ = State::Initializing;
    stats_ = {};
    metrics_ = {};
    for (std::size_t i = 0; i < fields().size(); ++i) {
        auto &metric = metrics_.channels[channelIndex(fields()[i].channel)];
        metric.requestedIntervalMs = settings_.bridgePolling && link_->deviceTimed()
                                         ? settings_.bridgePolling->requestedIntervalsMs[i]
                                     : fields()[i].channel == Channel::Coolant ? settings_.coolantMs
                                                                               : settings_.fastMs;
        if (settings_.bridgePolling && link_->deviceTimed()) {
            const auto channel = fields()[i].channel;
            metric.maximumExpectedIntervalMs =
                metric.requestedIntervalMs < bridgeRequestedIntervals()[channelIndex(channel)]
                    ? metric.requestedIntervalMs
                    : bridgeMaximumRequestInterval(channel) + BridgeReserveMs;
        }
    }
    exchange_ = {};
    error_.clear();
    pending_.reset();
    parser_.reset();
    accepted_ = {};
    model_.reset();
    nextRead_.fill(now + 300);
    initializedAt_ = now + 300;
    ratesStartedAt_ = now;
    cursor_ = 0;
    const auto sample = unavailable(now, session_);
    model_.apply(sample);
    if (!link_->deviceTimed())
        event("offline_boundary", now, 0,
              "New simulated ECU instance; queued prior experiment discarded explicitly; not a physical "
              "recovery "
              "guarantee");
    if (generation != generation_)
        return;
    if (onSample)
        onSample(sample);
    if (generation != generation_)
        return;
    exchange_.requestHex = hex(Initialization);
    exchange_.read = "Initialization: 11 bytes + 300 ms";
    exchange_.check = "Послідовність надіслано; відповіді/ACK та ідентифікації ECU немає";
    if (!link_->deviceTimed())
        event("tx_queued", now, 0, "initialization intent; no ECU ACK expected", Initialization);
    if (generation != generation_)
        return;
    const std::weak_ptr<int> alive = lifetime_;
    LinkCallbacks callbacks;
    callbacks.ready = [this, alive, generation](Time at) {
        if (alive.expired() || generation != generation_ || state_ != State::Initializing)
            return;
        state_ = State::Polling;
        nextRead_.fill(at);
        if (settings_.bridgePolling && link_->deviceTimed())
            bridgeScheduler_.reset(at, *settings_.bridgePolling);
        ratesStartedAt_ = at;
        event("initialization_wait_complete", at, 0,
              "Executor wait complete; ECU recognition not established");
    };
    // Keep stopped/faulted delivery callbacks alive to log all unassociated late RX.
    callbacks.received = [this, alive](auto bytes, Time at, Time measured) {
        if (!alive.expired())
            receive(bytes, at, measured);
    };
    callbacks.fault = [this, alive, generation](std::string reason, Time at, bool timeout) {
        if (!alive.expired() && generation == generation_ && state_ != State::Stopped)
            fail(std::move(reason), at, timeout);
    };
    callbacks.raw = [this, alive](const RawEvent &raw) {
        if (alive.expired())
            return;
        if (pending_ && raw.kind == "bridge_result" && raw.bridge)
            metrics_.firmwareOperation.add(raw.bridge->txElapsedMs + raw.bridge->rxElapsedMs);
        if (onRaw)
            onRaw(raw);
    };
    link_->start(session_, now, std::move(callbacks));
    if (!link_->deviceTimed())
        event("tx", now, 0, "initialization delivered to offline ECU; no ECU ACK expected", Initialization);
}
void Session::stop(Time now) {
    ++generation_;
    const auto transaction = pending_ ? pending_->transaction : 0;
    pending_.reset();
    parser_.reset();
    state_ = State::Stopped;
    model_.reset();
    link_->abort(now);
    event("stopped", now, transaction, "Context cancelled; remaining offline bytes retained for drain");
}
bool Session::setProfile(std::string_view profile, Time now) {
    stop(now);
    model_.reset();
    profileSelected_ = profile == ProfileId;
    if (!profileSelected_)
        error_ = "Невідомий reference-профіль";
    return profileSelected_;
}
void Session::setScenario(Scenario scenario) {
    if (lab_)
        lab_->setScenario(scenario);
}
void Session::setFaults(Faults faults) {
    if (lab_)
        lab_->setFaults(faults);
}
void Session::fail(std::string reason, Time now, bool timeout) {
    const auto transaction = pending_ ? pending_->transaction : 0;
    pending_.reset();
    parser_.reset(); // Intentionally does NOT reset OfflineLink or remove late responses.
    state_ = State::Faulted;
    if (timeout)
        ++stats_.timeouts;
    else
        ++stats_.corrupt;
    error_ = std::move(reason) + (link_->deviceTimed() ? "; потрібен новий лабораторний експеримент"
                                                       : "; потрібен новий offline-запуск");
    exchange_.check = error_;
    event("transaction_fault", now, transaction, error_);
}
bool Session::request(Operation operation, Read read, Time now, std::optional<Time> plannedAt) {
    const auto bytes = encode(operation, read);
    if (!bytes || state_ != State::Polling || pending_ || !link_->canExecute())
        return false;
    const auto generation = generation_;
    parser_.expect(read.length);
    const auto transaction = ++transaction_;
    pending_ = Pending{read, transaction, now + settings_.totalMs, now, std::nullopt};
    exchange_ = {};
    exchange_.requestHex = hex(*bytes);
    exchange_.check = "Очікування; host transaction ID відсутній у wire bytes";
    for (const auto &field : fields()) {
        if (field.read.address == read.address) {
            exchange_.read = field.name;
            exchange_.formulaSource = std::string(field.formula) + " | " + field.evidence;
        }
    }
    event("tx_queued", now, transaction, exchange_.read + "; " + exchange_.formulaSource, *bytes);
    if (generation != generation_ || !pending_ || state_ != State::Polling)
        return false;
    const bool responseQueued = link_->execute(*bytes, read, transaction, now);
    if (!link_->deviceTimed())
        event("tx", now, transaction, "delivered to offline ECU; " + exchange_.read, *bytes);
    if (generation != generation_)
        return responseQueued;
    if (!responseQueued) {
        ++metrics_.rejectedSubmissions;
        fail("DLC executor відхилив операцію або переповнив чергу", now, false);
        return false;
    }
    for (const auto &field : fields())
        if (field.read.address == read.address && field.read.length == read.length)
            metrics_.submittedRead(field.channel, now, plannedAt.value_or(now));
    return true;
}
void Session::receive(std::span<const std::uint8_t> bytes, Time now, Time measurementAt) {
    const auto generation = generation_;
    const auto transaction = pending_ ? pending_->transaction : 0;
    event("rx", now, transaction,
          pending_ ? "candidate under host context; not ECU transaction identity"
                   : "unassociated/late; ignored",
          bytes);
    if (generation != generation_)
        return;
    if (!pending_ || state_ != State::Polling) {
        stats_.ignored += bytes.size();
        return;
    }
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto byte = bytes[index];
        if (!exchange_.responseHex.empty())
            exchange_.responseHex += ' ';
        exchange_.responseHex += hex(std::span(&byte, 1));
        pending_->lastByte = now;
        const auto result = parser_.feed(byte);
        if (result == Parser::Result::Incomplete)
            continue;
        if (result != Parser::Result::Complete) {
            fail(result == Parser::Result::HeaderError   ? "Шум/неправильний header"
                 : result == Parser::Result::LengthError ? "Неправильна довжина"
                                                         : "Неправильна additive checksum",
                 now, false);
            return;
        }
        if (index + 1 != bytes.size()) {
            fail("Зайві DLC RX після повної відповіді", now, false);
            return;
        }
        auto sample = decode(pending_->read, parser_.payload(), now, session_, pending_->transaction);
        const auto requestStart = pending_->started;
        pending_.reset();
        parser_.reset();
        if (!sample) {
            fail("Не визначено decoder для відповіді", now, false);
            return;
        }
        if (link_->deviceTimed())
            sample->freshnessSince = measurementAt;
        model_.apply(*sample);
        model_.refresh(now);
        metrics_.acceptedSample(*sample, requestStart);
        metrics_.observe(model_, now, pendingBytes());
        ++stats_.accepted;
        for (std::size_t i = 0; i < ChannelCount; ++i)
            if ((sample->updatedMask & (1u << i)) != 0) {
                auto &times = accepted_[i];
                if (times.size() == 512)
                    times.pop_front();
                times.push_back(now);
            }
        exchange_.check = "Header/length/checksum OK; формула reference-derived, ECU не перевірено";
        event("transaction_complete", now, sample->request, exchange_.read + "; " + exchange_.check);
        if (generation != generation_)
            return;
        if (onSample)
            onSample(*sample);
        return;
    }
}
void Session::rates(Time now) {
    stats_.responseHz = 0;
    for (std::size_t i = 0; i < ChannelCount; ++i) {
        auto &times = accepted_[i];
        const auto window = settings_.bridgePolling && link_->deviceTimed() ? 10000 : 1000;
        while (!times.empty() && now >= times.front() && now - times.front() >= static_cast<Time>(window))
            times.pop_front();
        const auto elapsed = now >= ratesStartedAt_ ? now - ratesStartedAt_ : 0;
        const auto duration = std::min<Time>(window, elapsed);
        stats_.channelHz[i] = settings_.bridgePolling && link_->deviceTimed()
                                  ? duration ? times.size() * 1000.0 / duration : 0.0
                                  : static_cast<double>(times.size());
        stats_.responseHz += stats_.channelHz[i];
    }
}
void Session::tick(Time now) {
    const auto generation = generation_;
    model_.refresh(now);
    metrics_.observe(model_, now, pendingBytes());
    if (!link_->deviceTimed() && pending_ &&
        (now >= pending_->deadline || (pending_->lastByte && now >= *pending_->lastByte &&
                                       now - *pending_->lastByte >= settings_.interbyteMs))) {
        fail(now >= pending_->deadline ? "Загальний timeout; відповідь не можна однозначно прив'язати"
                                       : "Міжбайтовий timeout",
             now, true);
        if (generation != generation_)
            return;
    }
    link_->tick(now);
    if (generation != generation_)
        return;
    rates(now);
    if (state_ != State::Polling || pending_ || !link_->canExecute())
        return;
    if (settings_.bridgePolling && link_->deviceTimed()) {
        if (const auto index = bridgeScheduler_.selected(now)) {
            const auto planned = bridgeScheduler_.due();
            if (request(Operation::Read, fields()[*index].read, now, planned)) {
                bridgeScheduler_.submitted(now);
            }
        }
        return;
    }
    for (std::size_t offset = 0; offset < fields().size(); ++offset) {
        const auto index = (cursor_ + offset) % fields().size();
        if (now < nextRead_[index])
            continue;
        const auto planned = nextRead_[index];
        if (request(Operation::Read, fields()[index].read, now, planned)) {
            nextRead_[index] =
                now + (fields()[index].channel == Channel::Coolant ? settings_.coolantMs : settings_.fastMs);
            cursor_ = (index + 1) % fields().size();
        }
        break;
    }
}
} // namespace hd::dlc
