#include "honda_dlc/session.hpp"
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace hd::dlc {
Session::Session(Settings settings, FreshnessSettings freshness) : settings_(settings), model_(freshness) {
    if (!settings.totalMs || !settings.interbyteMs || settings.fastMs < 20 || !settings.coolantMs)
        throw std::invalid_argument("invalid offline DLC timing policy");
}
void Session::event(std::string kind, Time now, std::uint32_t transaction, std::string detail,
                    std::span<const std::uint8_t> bytes) {
    if (onRaw)
        onRaw({now, session_, transaction, std::move(kind), std::move(detail), {bytes.begin(), bytes.end()}});
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
    exchange_ = {};
    error_.clear();
    pending_.reset();
    parser_.reset();
    accepted_ = {};
    model_.reset();
    link_.newExperiment();
    nextRead_.fill(now + 300);
    initializedAt_ = now + 300;
    cursor_ = 0;
    const auto sample = unavailable(now, session_);
    model_.apply(sample);
    event("offline_boundary", now, 0,
          "New simulated ECU instance; queued prior experiment discarded explicitly; not a physical recovery "
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
    event("tx_queued", now, 0, "initialization intent; no ECU ACK expected", Initialization);
    if (generation != generation_)
        return;
    link_.send(Initialization, now);
    event("tx", now, 0, "initialization delivered to offline ECU; no ECU ACK expected", Initialization);
}
void Session::stop(Time now) {
    ++generation_;
    const auto transaction = pending_ ? pending_->transaction : 0;
    pending_.reset();
    parser_.reset();
    state_ = State::Stopped;
    model_.reset();
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
    link_.setScenario(scenario);
}
void Session::setFaults(Faults faults) {
    link_.setFaults(faults);
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
    error_ = std::move(reason) + "; потрібен новий offline-запуск";
    exchange_.check = error_;
    event("transaction_fault", now, transaction, error_);
}
bool Session::request(Operation operation, Read read, Time now) {
    const auto bytes = encode(operation, read);
    if (!bytes || state_ != State::Polling || pending_)
        return false;
    const auto generation = generation_;
    parser_.expect(read.length);
    const auto transaction = ++transaction_;
    pending_ = Pending{read, transaction, now + settings_.totalMs, std::nullopt};
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
    const bool responseQueued = link_.send(*bytes, now);
    event("tx", now, transaction, "delivered to offline ECU; " + exchange_.read, *bytes);
    if (generation != generation_)
        return responseQueued;
    if (!responseQueued) {
        fail("Переповнення bounded offline RX queue", now, false);
        return false;
    }
    return true;
}
void Session::receive(std::span<const std::uint8_t> bytes, Time now) {
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
    for (auto byte : bytes) {
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
        const auto sample = decode(pending_->read, parser_.payload(), now, session_, pending_->transaction);
        pending_.reset();
        parser_.reset();
        if (!sample) {
            fail("Не визначено decoder для відповіді", now, false);
            return;
        }
        model_.apply(*sample);
        ++stats_.accepted;
        for (std::size_t i = 0; i < ChannelCount; ++i)
            if ((sample->updatedMask & (1u << i)) != 0) {
                auto &times = accepted_[i];
                if (times.size() == 64)
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
        while (!times.empty() && now >= times.front() && now - times.front() >= 1000)
            times.pop_front();
        stats_.channelHz[i] = static_cast<double>(times.size());
        stats_.responseHz += stats_.channelHz[i];
    }
}
void Session::tick(Time now) {
    const auto generation = generation_;
    model_.refresh(now);
    if (pending_ && (now >= pending_->deadline || (pending_->lastByte && now >= *pending_->lastByte &&
                                                   now - *pending_->lastByte >= settings_.interbyteMs))) {
        fail(now >= pending_->deadline ? "Загальний timeout; відповідь не можна однозначно прив'язати"
                                       : "Міжбайтовий timeout",
             now, true);
        if (generation != generation_)
            return;
    }
    link_.tick(now, [this](auto bytes, Time at) { receive(bytes, at); });
    if (generation != generation_)
        return;
    rates(now);
    if (state_ == State::Initializing && now >= initializedAt_) {
        state_ = State::Polling;
        event("initialization_wait_complete", now, 0,
              "Only local wait complete; ECU recognition not established");
        if (generation != generation_)
            return;
    }
    if (state_ != State::Polling || pending_)
        return;
    for (std::size_t offset = 0; offset < fields().size(); ++offset) {
        const auto index = (cursor_ + offset) % fields().size();
        if (now < nextRead_[index])
            continue;
        nextRead_[index] =
            now + (fields()[index].channel == Channel::Coolant ? settings_.coolantMs : settings_.fastMs);
        cursor_ = (index + 1) % fields().size();
        request(Operation::Read, fields()[index].read, now);
        break;
    }
}
} // namespace hd::dlc
