#ifndef HD_BENCH_RESPONDER_ENDPOINT_HPP
#define HD_BENCH_RESPONDER_ENDPOINT_HPP
#include "../nano_dlc_bridge_lab/dlc_port.hpp"
#include "../shared/bench_wire.hpp"
#include <stddef.h>
namespace hd_bench {
struct ResponderCounters {
    uint32_t usbFrames, parserErrors, lineErrors, requests, replies, canceled, txOverflows;
};
class ResponderEndpoint {
  public:
    explicit ResponderEndpoint(hd_bridge::DlcPort &port);
    void reset(uint32_t now);
    void receive(uint8_t byte, uint32_t now);
    void tick(uint32_t now);
    size_t read(uint8_t *output, size_t capacity);
    size_t available() const { return txSize_; }
    size_t buffered() const { return rxSize_; }
    uint32_t generation() const { return generation_; }
    bool armed() const { return armed_; }
    bool quiescent() const { return !armed_ && !quiescing_ && port_.txIdle(0) && !replySize_; }
    const ResponderCounters &counters() const { return counters_; }

  private:
    void resetBuffers(uint32_t now);
    void dispatch(uint8_t count, uint32_t now);
    void rawByte(uint8_t value, uint32_t observedAt);
    void discard(uint8_t count);
    bool enqueue(const uint8_t *bytes, uint8_t count);
    void reply(uint8_t type, uint32_t session, uint32_t request, const uint8_t *bytes, uint8_t count,
               bool cache = true);
    void ack(uint8_t command, uint8_t status, uint32_t session, uint32_t request, bool cache = true);
    void requestQuiesce(uint32_t request);
    void finishQuiesce();
    hd_bridge::DlcPort &port_;
    uint8_t rx_[hd_bridge::MaxFrame], rxSize_;
    uint8_t tx_[hd_bridge::TxCapacity], txHead_, txSize_;
    uint8_t lastRequest_[hd_bridge::MaxFrame], lastRequestSize_;
    uint8_t lastReply_[hd_bridge::MaxFrame], lastReplySize_;
    uint8_t input_[11], inputSize_, reply_[hd_bridge::MaxDlcRx], replySize_, replyAt_;
    uint32_t session_, lastId_, generation_, quiesceRequest_, inputAt_, initAt_, replyAtMs_;
    uint16_t delay_, gap_, activeGap_;
    uint8_t scenario_, fault_;
    bool bound_, armed_, initialized_, quiescing_, responseStarted_, lineFaulted_;
    ResponderCounters counters_;
};
} // namespace hd_bench
#endif
