#ifndef HD_REFERENCE_FIXTURES_HPP
#define HD_REFERENCE_FIXTURES_HPP
#include <stdint.h>
#include <string.h>
namespace hd_bridge {
// Single source for M2b virtual and M3a external raw fixtures. These are independent
// reference-derived test bytes, not captured ECU measurements or physical quantities.
static const uint8_t WakeBytes[] = {0x68, 0x6a, 0xf5, 0xaf, 0xbf, 0xb3, 0xb2, 0xc1, 0xdb, 0xb3, 0xe9};
inline uint8_t rawFixture(uint8_t scenario, uint8_t address, uint8_t readLength, uint8_t *out) {
    static const uint8_t Rpm[3][5] = {{0, 5, 9, 0xc3, 0x2f}, {0, 5, 4, 0xe1, 0x16}, {0, 5, 0xff, 0xff, 0xfd}};
    static const uint8_t Ect[3][4] = {{0, 4, 0x40, 0xbc}, {0, 4, 0x20, 0xdc}, {0, 4, 0xff, 0xfd}};
    static const uint8_t Tps[3][4] = {{0, 4, 0x58, 0xa4}, {0, 4, 0xae, 0x4e}, {0, 4, 0x18, 0xe4}};
    const uint8_t index = scenario == 3 ? 2 : scenario == 1 ? 1 : 0;
    if (address == 0 && readLength == 2) {
        memcpy(out, Rpm[index], 5);
        return 5;
    }
    if (address == 0x10 && readLength == 1) {
        memcpy(out, Ect[index], 4);
        return 4;
    }
    if (address == 0x14 && readLength == 1) {
        memcpy(out, Tps[index], 4);
        return 4;
    }
    return 0;
}
} // namespace hd_bridge
#endif
