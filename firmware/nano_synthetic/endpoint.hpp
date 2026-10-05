#ifndef HD_NANO_ENDPOINT_HPP
#define HD_NANO_ENDPOINT_HPP

#include <stddef.h>
#include <stdint.h>

// Deliberately C++11, shared verbatim by AVR firmware and native tests. No Arduino,
// Qt, allocator, host endian or sizeof(int) assumptions are used in this module.
namespace hd_nano {
enum { MaxPayload = 64, MaxFrame = 79, TxCapacity = 158, ChannelCount = 7 };
struct Counters {
    uint32_t frames;
    uint32_t parserErrors;
    uint32_t txOverflows;
    uint32_t duplicates;
};

class Endpoint {
public:
    Endpoint();
    void reset(uint32_t now);
    void receive(uint8_t byte, uint32_t now);
    void tick(uint32_t now);
    size_t read(uint8_t* destination, size_t capacity);
    size_t available() const { return txSize_; }
    size_t buffered() const { return rxSize_; }
    const Counters& counters() const { return counters_; }

private:
    bool candidate(uint8_t at) const;
    void discard(uint8_t count);
    void drain(uint32_t now);
    void dispatch(uint8_t length, uint32_t now);
    void enqueue(const uint8_t* bytes, uint8_t length);
    uint8_t response(uint8_t* output, uint8_t type, uint32_t session,
                     uint32_t request, const uint8_t* payload, uint8_t length) const;
    void error(uint8_t code, uint32_t session, uint32_t request);
    void snapshot(uint8_t* payload, uint32_t now) const;

    uint8_t rx_[MaxFrame];
    uint8_t rxSize_;
    uint8_t tx_[TxCapacity];
    uint8_t txHead_;
    uint8_t txSize_;
    uint8_t lastRequest_[MaxFrame];
    uint8_t lastRequestSize_;
    uint8_t lastResponse_[MaxFrame];
    uint8_t lastResponseSize_;
    uint8_t delayed_[MaxFrame];
    uint8_t delayedSize_;
    uint32_t delayedAt_;
    uint16_t delayedWait_;
    bool initialized_;
    uint32_t session_;
    uint32_t lastId_;
    uint8_t scenario_;
    uint32_t epoch_;
    uint32_t seed_;
    int16_t manual_[ChannelCount];
    uint8_t quality_[ChannelCount];
    bool silent_;
    uint16_t delayMs_;
    uint8_t once_;
    Counters counters_;
};
} // namespace hd_nano
#endif
