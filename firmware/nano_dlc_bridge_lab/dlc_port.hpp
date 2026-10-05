#ifndef HD_BRIDGE_DLC_PORT_HPP
#define HD_BRIDGE_DLC_PORT_HPP
#include <stdint.h>
namespace hd_bridge {
// One byte at a time, with an explicit completion boundary. No GPIO implementation exists.
class DlcPort {
  public:
    virtual bool writeByte(uint8_t byte, uint32_t now) = 0;
    virtual bool txIdle(uint32_t now) const = 0;
    // observedAt uses the same device clock. The virtual port reports its scheduled
    // byte event, preserving gaps even when the host delivers an outer frame later.
    virtual bool readByte(uint8_t &byte, uint32_t now, uint32_t &observedAt) = 0;

  protected:
    // Non-owning reference interface: no deleting destructor or allocator runtime.
    ~DlcPort() {}
};
} // namespace hd_bridge
#endif
