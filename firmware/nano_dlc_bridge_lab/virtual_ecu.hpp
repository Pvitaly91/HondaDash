#ifndef HD_BRIDGE_VIRTUAL_ECU_HPP
#define HD_BRIDGE_VIRTUAL_ECU_HPP
#include "dlc_port.hpp"
#include "wire.hpp"
namespace hd_bridge {
class VirtualHondaEcu : public DlcPort {
  public:
    VirtualHondaEcu();
    void newExperiment();
    bool configure(uint8_t scenario, uint8_t fault, uint16_t delay, uint16_t gap);
    bool writeByte(uint8_t byte, uint32_t now);
    bool txIdle(uint32_t) const { return true; }
    bool readByte(uint8_t &byte, uint32_t now, uint32_t &observedAt);
    uint8_t pending() const { return uint8_t(replySize_ - replyAt_); }
    uint32_t txBytes() const { return txBytes_; }
    uint8_t pendingRx() const { return pending(); }
    void resetBackend() { *this = VirtualHondaEcu(); }
    void newExperimentBackend() { newExperiment(); }
    bool configureBackend(uint8_t scenario, uint8_t fault, uint16_t delay, uint16_t gap) {
        return configure(scenario, fault, delay, gap);
    }

  private:
    void dispatch(uint32_t now);
    uint8_t input_[11], inputSize_;
    uint8_t reply_[MaxDlcRx], replySize_, replyAt_;
    uint32_t replyEpoch_, initAt_, txBytes_;
    uint16_t delay_, gap_, activeDelay_, activeGap_;
    uint8_t scenario_, fault_;
    bool initialized_;
};
} // namespace hd_bridge
#endif
