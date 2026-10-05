#include "transport/in_memory_transport.hpp"
#include "protocol/device_extension.hpp"
#include <algorithm>
namespace hd {
void InMemoryTransport::start(TransportCallbacks cb, Time n) {
    close();
    callbacks_ = std::move(cb);
    emulator_.start(n);
    open_ = true;
    auto callback = callbacks_.opened;
    if (callback)
        callback(n);
}
void InMemoryTransport::close() {
    ++generation_;
    open_ = false;
    callbacks_ = {};
    deliveries_.clear();
    parser_.reset();
    session_ = lastRequest_ = 0;
    lastBytes_.clear();
    lastReply_.clear();
}
std::vector<EmulatedReply> InMemoryTransport::dispatch(const Frame &f, Time n) {
    auto error = [&](std::uint8_t code) {
        return std::vector<EmulatedReply>{{0, encode({ErrorResponse, f.session, f.request, {code}})}};
    };
    auto bytes = encode(f);
    if (f.type == Hello && !f.payload.empty())
        return error(2); // A rejected HELLO cannot change identity or duplicate ordering.
    if (f.type == Hello && f.payload.empty() && f.session != session_) {
        session_ = f.session;
        lastRequest_ = 0;
        lastBytes_.clear();
        lastReply_.clear();
        deliveries_.clear();
    }
    if (!session_ || f.session != session_)
        return error(3);
    if (f.request == lastRequest_)
        return bytes == lastBytes_ ? lastReply_ : error(5);
    if (f.request < lastRequest_)
        return error(5);
    std::vector<EmulatedReply> replies;
    Frame ack{static_cast<std::uint8_t>(f.type | 0x80), f.session, f.request, {0}};
    const auto &p = f.payload;
    auto bad = [&] { replies = error(2); };
    if (f.type == Hello || f.type == ReadSnapshot)
        replies = emulator_.consume(bytes, n);
    else if (f.type == GetDeviceInfo) {
        if (!p.empty())
            bad();
        else {
            auto cap = emulator_.capabilities();
            ack.payload = {
                1, 1, 0, 2, 0, static_cast<std::uint8_t>(cap), static_cast<std::uint8_t>(cap >> 8)};
            const std::string endpoint = "desktop-emulator";
            ack.payload.insert(ack.payload.end(), endpoint.begin(), endpoint.end());
        }
    } else if (f.type >= SetScenario && f.type <= SetQuality) {
        if (!(emulator_.capabilities() & (1u << (f.type - SetScenario))))
            replies = error(1);
        else if (f.type == SetScenario) {
            if (p.size() != 5 || p[0] > 3)
                bad();
            else {
                std::uint32_t seed = 0;
                for (unsigned i = 0; i < 4; ++i)
                    seed |= std::uint32_t(p[i + 1]) << (8 * i);
                emulator_.setSeed(seed);
                emulator_.setScenario(static_cast<Scenario>(p[0]), n);
            }
        } else if (f.type == SetManual) {
            if (p.size() != 14)
                bad();
            else {
                constexpr double scales[]{1, 10, 10, 10, 10, 10, 100};
                std::array<double, ChannelCount> values{};
                bool valid = true;
                for (std::size_t i = 0; i < ChannelCount; ++i) {
                    auto word = std::uint16_t(p[2 * i]) | (std::uint16_t(p[2 * i + 1]) << 8);
                    auto signedWord = word >= 32768 ? static_cast<int>(word) - 65536 : static_cast<int>(word);
                    values[i] = signedWord / scales[i];
                    auto &c = channelInfo(static_cast<Channel>(i));
                    valid = valid && values[i] >= c.min && values[i] <= c.max;
                }
                if (!valid)
                    bad();
                else
                    for (std::size_t i = 0; i < ChannelCount; ++i)
                        emulator_.setManual(static_cast<Channel>(i), values[i]);
            }
        } else if (f.type == SetFaults) {
            if (p.size() != 4 || p[0] > 1 || p[3] > 3 || (unsigned(p[1]) | (unsigned(p[2]) << 8)) > 5000)
                bad();
            else
                emulator_.faults() = {p[0] != 0, Time(p[1]) | (Time(p[2]) << 8), (p[3] & 1) != 0,
                                      (p[3] & 2) != 0};
        } else {
            if (p.size() != 7 ||
                std::any_of(p.begin(), p.end(), [](auto q) { return q != 1 && q != 3 && q != 4; }))
                bad();
            else
                for (std::size_t i = 0; i < ChannelCount; ++i)
                    emulator_.setChannelQuality(static_cast<Channel>(i), static_cast<Quality>(p[i]));
        }
    } else
        replies = error(1);
    if (replies.empty() && f.type != ReadSnapshot && f.type != Hello)
        replies = {{0, encode(ack)}};
    lastRequest_ = f.request;
    lastBytes_ = std::move(bytes);
    lastReply_ = replies;
    return replies;
}
bool InMemoryTransport::send(std::span<const std::uint8_t> bytes, Time n) {
    if (!open_)
        return false;
    const auto generation = generation_;
    auto raw = callbacks_.raw;
    if (raw)
        raw("TX", bytes, n);
    if (generation != generation_)
        return false;
    for (const auto &f : parser_.feed(bytes))
        for (const auto &reply : dispatch(f, n)) {
            constexpr std::size_t pattern[]{1, 3, 2, 7, 4, 11};
            std::size_t fragments = 0;
            for (std::size_t remaining = reply.bytes.size(), index = f.request % 6; remaining;) {
                remaining -= std::min(pattern[index++ % 6], remaining);
                ++fragments;
            }
            if (deliveries_.size() + fragments > 256) {
                auto error = callbacks_.error;
                close();
                if (error)
                    error("In-memory RX delivery overflow", n);
                return false;
            }
            std::size_t offset = 0, index = f.request % 6;
            Time due = n + reply.delayMs;
            // A byte stream may fragment frames, but must never interleave them.
            // Reserve one contiguous free interval. An immediate control ACK can
            // precede a not-yet-due snapshot, or follow its entire remaining tail.
            for (const auto &delivery : deliveries_) {
                if (delivery.due < due)
                    continue;
                if (delivery.due - due >= fragments)
                    break;
                due = delivery.due + 1;
            }
            while (offset < reply.bytes.size()) {
                auto count = std::min(pattern[index++ % 6], reply.bytes.size() - offset);
                Delivery d{due++,
                           {reply.bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                            reply.bytes.begin() + static_cast<std::ptrdiff_t>(offset + count)}};
                offset += count;
                auto pos = std::upper_bound(deliveries_.begin(), deliveries_.end(), d.due,
                                            [](Time t, const Delivery &item) { return t < item.due; });
                deliveries_.insert(pos, std::move(d));
            }
        }
    auto sent = callbacks_.sent;
    if (sent)
        sent(n);
    return true;
}
void InMemoryTransport::tick(Time n) {
    const auto generation = generation_;
    unsigned budget = 64;
    while (open_ && budget-- && !deliveries_.empty() && deliveries_.front().due <= n) {
        auto d = std::move(deliveries_.front());
        deliveries_.pop_front();
        auto raw = callbacks_.raw;
        if (raw)
            raw("RX", d.bytes, n);
        if (generation != generation_)
            return;
        auto received = callbacks_.received;
        if (received)
            received(d.bytes, n);
        if (generation != generation_)
            return;
    }
}
} // namespace hd
