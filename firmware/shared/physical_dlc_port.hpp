#ifndef HD_PHYSICAL_DLC_PORT_HPP
#define HD_PHYSICAL_DLC_PORT_HPP
#include "../nano_dlc_bridge_lab/dlc_port.hpp"
#include "../nano_dlc_bridge_lab/wire.hpp"
#include "one_wire.hpp"
namespace hd_bench {
// Identical adapter for AvrPort and the bit-level test HAL. Neither adapter knows
// a host request ID or has access to fixture bytes. RX time belongs to the MCU.
template <class Port> class PhysicalDlcPort : public hd_bridge::DlcPort {
  public:
    explicit PhysicalDlcPort(Port &port) : port_(port), txBytes_(0) {}
    bool writeByte(uint8_t byte, uint32_t) {
        if (!port_.sendByte(byte))
            return false;
        if (txBytes_ != UINT32_MAX)
            ++txBytes_;
        return true;
    }
    bool txIdle(uint32_t) const { return port_.txComplete(); }
    bool readByte(uint8_t &byte, uint32_t, uint32_t &observedAt) { return port_.read(byte, observedAt); }
    void abortTx() { port_.abort(); }
    bool lineIdle(uint32_t) const { return port_.idleHigh() && port_.txComplete(); }
    uint8_t pendingRx() const { return port_.rxPending(); }
    void clearFault() { port_.clearFault(); }
    uint32_t txBytes() const { return txBytes_; }
    bool hasPhysicalTxAccounting() const { return true; }
    uint16_t completedTxSequence() const { return port_.completedTxSequence(); }
    uint8_t driverDiagnostics(uint8_t *output, uint8_t capacity) const {
        if (capacity < 22)
            return 0;
        const hd_onewire::Counters c = port_.counters();
        const uint16_t values[] = {c.rxBytes,    c.txBytes,    c.echoBytes,  c.falseStarts,
                                   c.framing,    c.rxOverflow, c.txOverflow, c.stuckLow,
                                   c.collisions, c.timing,     c.lineBusy};
        for (uint8_t i = 0; i < 11; ++i)
            hd_bridge::store16(output + 2 * i, values[i]);
        return 22;
    }
    uint8_t takeError() {
        switch (port_.takeError()) {
        case hd_onewire::NoError:
            return hd_bridge::Ok;
        case hd_onewire::Framing:
            return hd_bridge::DlcFraming;
        case hd_onewire::RxOverflow:
            return hd_bridge::DlcRxOverflow;
        case hd_onewire::TxOverflow:
            return hd_bridge::Overflow;
        case hd_onewire::Collision:
            return hd_bridge::DlcCollision;
        default:
            return hd_bridge::DlcLine;
        }
    }

  private:
    Port &port_;
    uint32_t txBytes_;
};
} // namespace hd_bench
#endif
