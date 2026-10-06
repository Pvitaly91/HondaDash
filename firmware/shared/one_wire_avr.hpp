#ifndef HD_ONE_WIRE_AVR_HPP
#define HD_ONE_WIRE_AVR_HPP
#include "one_wire.hpp"
#ifdef __AVR__
namespace hd_onewire {
// One owner per MCU. Timer1 and ICP1/D8 are exclusively reserved for this port.
class AvrPort {
  public:
    void begin();
    void service();
    bool sendByte(uint8_t byte);
    bool txComplete() const;
    bool read(uint8_t &byte, uint32_t &observedAtMs);
    void abort();
    Error takeError();
    void clearFault();
    bool idleHigh() const;
    uint8_t rxPending() const;
    uint16_t completedTxSequence() const;
    Counters counters() const;
};
}
#endif
#endif
