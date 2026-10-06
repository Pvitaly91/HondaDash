#ifndef HD_BRIDGE_DLC_PORT_HPP
#define HD_BRIDGE_DLC_PORT_HPP
#include <stdint.h>
namespace hd_bridge {
// One byte at a time, with an explicit physical TX completion boundary.
class DlcPort {
  public:
    virtual bool writeByte(uint8_t byte, uint32_t now) = 0;
    virtual bool txIdle(uint32_t now) const = 0;
    // observedAt uses the same device clock. The virtual port reports its scheduled
    // byte event, preserving gaps even when the host delivers an outer frame later.
    virtual bool readByte(uint8_t &byte, uint32_t now, uint32_t &observedAt) = 0;
    // Receiver errors are distinct from an empty queue. Values are bridge Status codes.
    // Defaults preserve the existing virtual and byte-level test ports.
    virtual uint8_t takeError() { return 0; }
    virtual void abortTx() {}
    virtual bool lineIdle(uint32_t) const { return true; }
    virtual uint8_t pendingRx() const { return 0; }
    virtual void clearFault() {}
    virtual uint32_t txBytes() const { return 0; }
    virtual bool hasPhysicalTxAccounting() const { return false; }
    virtual uint16_t completedTxSequence() const { return 0; }
    virtual uint8_t driverDiagnostics(uint8_t *, uint8_t) const { return 0; }
    // Laboratory backend controls. The physical backend never resets its external peer.
    virtual void resetBackend() { abortTx(); }
    virtual void newExperimentBackend() { clearFault(); }
    virtual bool configureBackend(uint8_t, uint8_t, uint16_t, uint16_t) { return false; }

  protected:
    // Non-owning reference interface: no deleting destructor or allocator runtime.
    ~DlcPort() {}
};
} // namespace hd_bridge
#endif
