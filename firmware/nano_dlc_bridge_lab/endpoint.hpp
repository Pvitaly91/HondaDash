#ifndef HD_BRIDGE_ENDPOINT_HPP
#define HD_BRIDGE_ENDPOINT_HPP
#include "transaction_engine.hpp"
#include "virtual_ecu.hpp"
#include <stddef.h>
namespace hd_bridge {
struct Counters {
    uint32_t frames, parserErrors, txOverflows, duplicates, lostEvents;
};
class BridgeEndpoint {
  public:
    BridgeEndpoint();
    BridgeEndpoint(const BridgeEndpoint &) = delete;
    BridgeEndpoint &operator=(const BridgeEndpoint &) = delete;
    void reset(uint32_t now);
    void receive(uint8_t byte, uint32_t now);
    void tick(uint32_t now);
    size_t read(uint8_t *output, size_t capacity);
    size_t available() const { return txSize_; }
    size_t buffered() const { return rxSize_; }
    const Counters &counters() const { return counters_; }
    uint8_t state() const { return engine_.state(); }
    uint32_t generation() const { return generation_; }
    uint32_t dlcTxBytes() const { return ecu_.txBytes(); }
    uint8_t pendingDlcRx() const { return ecu_.pending(); }

  private:
    void dispatch(uint8_t frameSize, uint32_t now);
    void discard(uint8_t count);
    void reply(uint8_t type, uint32_t session, uint32_t request, const uint8_t *payload, uint8_t size,
               bool cache = false);
    bool enqueue(const uint8_t *bytes, uint8_t size);
    void failOverflow(uint32_t now);
    void publishResult();
    void ack(uint8_t type, uint8_t command, uint8_t status, uint32_t session, uint32_t request,
             bool cache = false);
    VirtualHondaEcu ecu_;
    TransactionEngine engine_;
    uint8_t rx_[MaxFrame], rxSize_, tx_[TxCapacity], txHead_, txSize_;
    uint8_t lastRequest_[MaxFrame], lastRequestSize_, lastReply_[MaxFrame], lastReplySize_;
    uint32_t session_, lastId_, activeId_, generation_, experimentAt_;
    uint16_t eventSequence_;
    bool bound_;
    Counters counters_;
};
} // namespace hd_bridge
#endif
