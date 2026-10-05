#include "application/session.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hd {
Session::Session(SessionSettings settings, FreshnessSettings freshness) : settings_(settings), model_(freshness) {
    if (!settings.pollMs || !settings.timeoutMs || !settings.helloAttempts) throw std::invalid_argument("Invalid session settings");
}
void Session::event(std::string kind, Time now, std::uint32_t request, std::string detail, std::vector<std::uint8_t> bytes) {
    if (onRaw) onRaw(RawEvent{now,sessionId_,request,std::move(kind),std::move(detail),std::move(bytes)});
}
void Session::start(Time now) {
    stop(now);
    if (sessionId_ == std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("Session IDs exhausted");
    ++sessionId_;
    nextRequest_ = 0;
    helloSent_ = 0;
    stats_ = {};
    emulator_.start(now);
    state_ = SessionState::Handshaking;
    event("start",now);
    send(Hello,now);
}
void Session::stop(Time now) {
    if (state_ != SessionState::Stopped) event("stop",now);
    state_ = SessionState::Stopped;
    pending_.reset();
    deliveries_.clear();
    acceptedTimes_.clear();
    parser_.reset();
    emulator_.resetTransport();
    model_.reset();
    stats_.responseHz = 0;
}
void Session::send(std::uint8_t type, Time now) {
    if (nextRequest_ == std::numeric_limits<std::uint32_t>::max()) {
        state_ = SessionState::Faulted;
        event("error",now,0,"Request IDs exhausted");
        return;
    }
    const auto request = ++nextRequest_;
    pending_ = Pending{request,type,now + settings_.timeoutMs};
    nextPoll_ = now + settings_.pollMs;
    if (type == Hello) ++helloSent_;
    const auto bytes = encode(Frame{type,sessionId_,request,{}});
    event("TX",now,request,{},bytes);
    for (const auto& reply : emulator_.consume(bytes,now)) {
        // Vary the fragmentation deterministically; no callback is a packet boundary.
        std::vector<Delivery> fragments;
        const std::size_t pattern[] = {1,3,2,7,4,11};
        std::size_t offset = 0, index = request % 6;
        Time due = now + reply.delayMs;
        while (offset < reply.bytes.size()) {
            const auto size = std::min(pattern[index++ % 6],reply.bytes.size()-offset);
            fragments.push_back({due++,{reply.bytes.begin()+static_cast<std::ptrdiff_t>(offset),reply.bytes.begin()+static_cast<std::ptrdiff_t>(offset+size)}});
            offset += size;
        }
        if (deliveries_.size()+fragments.size() > 256) {
            ++stats_.ignored;
            event("transport_overflow",now,request,"Bounded delivery queue is full");
            continue;
        }
        for (auto& fragment : fragments) {
            auto pos = std::upper_bound(deliveries_.begin(),deliveries_.end(),fragment.due,
                [](Time dueTime,const Delivery& item){return dueTime < item.due;});
            deliveries_.insert(pos,std::move(fragment));
        }
    }
}
void Session::receive(std::span<const std::uint8_t> bytes, Time now) {
    if (state_ == SessionState::Stopped || state_ == SessionState::Faulted) return;
    event("RX",now,pending_ ? pending_->request : 0,{},std::vector<std::uint8_t>(bytes.begin(),bytes.end()));
    const auto before = parser_.errors();
    const auto frames = parser_.feed(bytes);
    stats_.corrupt += parser_.errors()-before;
    for (const auto& frame : frames) {
        if (!pending_ || frame.session != sessionId_ || frame.request != pending_->request || now >= pending_->deadline) {
            ++stats_.ignored;
            event("ignored",now,frame.request,"Unmatched, duplicate, old-session or expired response");
            continue;
        }
        if (pending_->type == Hello && frame.type == HelloResponse) {
            const std::string profile = "synthetic-demo-v1";
            const std::vector<std::uint8_t> expected = [&]{
                std::vector<std::uint8_t> result{1}; result.insert(result.end(),profile.begin(),profile.end()); return result;
            }();
            if (frame.payload != expected) {
                ++stats_.corrupt;
                event("invalid_hello",now,frame.request);
                continue;
            }
            pending_.reset();
            state_ = SessionState::Running;
            nextPoll_ = now;
            event("ready",now,frame.request);
        } else if (pending_->type == ReadSnapshot && frame.type == SnapshotResponse) {
            const auto sample = decodeSnapshot(frame,now);
            if (!sample) { ++stats_.corrupt; event("invalid_snapshot",now,frame.request); continue; }
            pending_.reset();
            model_.apply(*sample);
            ++stats_.accepted;
            acceptedTimes_.push_back(now);
            if (onSample) onSample(*sample);
        } else {
            ++stats_.ignored;
            event("unexpected_response",now,frame.request);
        }
    }
}
void Session::tick(Time now) {
    model_.refresh(now);
    if (state_ == SessionState::Stopped || state_ == SessionState::Faulted) return;
    while (!deliveries_.empty() && deliveries_.front().due <= now) {
        auto delivery = std::move(deliveries_.front());
        deliveries_.pop_front();
        receive(delivery.bytes,now);
    }
    if (pending_ && now >= pending_->deadline) {
        ++stats_.timeouts;
        event("timeout",now,pending_->request);
        pending_.reset();
        // Discard an incomplete expired frame so it cannot hold a future response hostage.
        parser_.reset();
        if (state_ == SessionState::Handshaking) {
            if (helloSent_ >= settings_.helloAttempts) {
                state_ = SessionState::Faulted;
                deliveries_.clear();
                event("handshake_failed",now);
            } else send(Hello,now);
        }
    }
    while (!acceptedTimes_.empty() && now-acceptedTimes_.front() >= 1000) acceptedTimes_.pop_front();
    stats_.responseHz = static_cast<double>(acceptedTimes_.size());
    if (state_ == SessionState::Running && !pending_ && now >= nextPoll_) send(ReadSnapshot,now);
}
}
