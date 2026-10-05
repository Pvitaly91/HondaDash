#ifndef HD_BRIDGE_TRANSACTION_ENGINE_HPP
#define HD_BRIDGE_TRANSACTION_ENGINE_HPP
#include "dlc_port.hpp"
#include "wire.hpp"
namespace hd_bridge {
struct DlcResult {
    uint8_t status, command, address, readLength, txLength, rxLength;
    uint16_t txElapsed, responseElapsed, maxGap;
    uint8_t tx[11], rx[MaxDlcRx];
};
class TransactionEngine {
  public:
    explicit TransactionEngine(DlcPort &port);
    TransactionEngine(const TransactionEngine &) = delete;
    TransactionEngine &operator=(const TransactionEngine &) = delete;
    void newExperiment();
    bool initialize(uint32_t now);
    bool execute(const uint8_t *request, uint8_t length, uint8_t expected, uint32_t now);
    void abort(uint32_t now, uint8_t status = Aborted);
    void tick(uint32_t now);
    bool takeResult(DlcResult &result);
    uint8_t takeLate(uint8_t *output, uint8_t capacity);
    uint8_t state() const { return state_; }
    uint8_t activeCommand() const { return active_ ? result_.command : 0; }
    static bool allowed(const uint8_t *request, uint8_t length, uint8_t expected);

  private:
    void begin(uint8_t command, const uint8_t *bytes, uint8_t length, uint32_t now);
    void finish(uint8_t status, uint32_t now);
    DlcPort &port_;
    DlcResult result_;
    uint32_t started_, txDone_, lastRx_;
    uint8_t state_, txAt_, txSize_, expected_, late_[MaxDlcRx], lateSize_;
    bool active_, transmitting_, complete_, resultReady_;
};
} // namespace hd_bridge
#endif
