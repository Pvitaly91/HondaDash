#include "virtual_ecu.hpp"
#include <string.h>
namespace hd_bridge {
namespace {
const uint8_t Wake[] = {0x68, 0x6a, 0xf5, 0xaf, 0xbf, 0xb3, 0xb2, 0xc1, 0xdb, 0xb3, 0xe9};
// Independent reference-derived fixtures, never physical quantities or ECU captures.
const uint8_t RpmA[] = {0x00, 0x05, 0x09, 0xc3, 0x2f};
const uint8_t RpmB[] = {0x00, 0x05, 0x04, 0xe1, 0x16};
const uint8_t EctA[] = {0x00, 0x04, 0x40, 0xbc};
const uint8_t EctB[] = {0x00, 0x04, 0x20, 0xdc};
const uint8_t TpsA[] = {0x00, 0x04, 0x58, 0xa4};
const uint8_t TpsB[] = {0x00, 0x04, 0xae, 0x4e};
const uint8_t RpmBoundary[] = {0x00, 0x05, 0xff, 0xff, 0xfd};
const uint8_t EctBoundary[] = {0x00, 0x04, 0xff, 0xfd};
const uint8_t TpsBoundary[] = {0x00, 0x04, 0x18, 0xe4};
} // namespace
VirtualHondaEcu::VirtualHondaEcu()
    : inputSize_(0), replySize_(0), replyAt_(0), replyEpoch_(0), initAt_(0), txBytes_(0), delay_(0), gap_(0),
      activeDelay_(0), activeGap_(0), scenario_(0), fault_(0), initialized_(false) {}
void VirtualHondaEcu::newExperiment() {
    inputSize_ = replySize_ = replyAt_ = 0;
    initialized_ = false;
}
bool VirtualHondaEcu::configure(uint8_t scenario, uint8_t fault, uint16_t delay, uint16_t gap) {
    if ((scenario != 0 && scenario != 1 && scenario != 3) || fault > Trailing || delay > 10000 || gap > 10000)
        return false;
    scenario_ = scenario;
    fault_ = fault;
    delay_ = delay;
    gap_ = gap;
    return true;
}
bool VirtualHondaEcu::writeByte(uint8_t byte, uint32_t now) {
    if (inputSize_ == sizeof input_)
        return false;
    input_[inputSize_++] = byte;
    if (txBytes_ != UINT32_MAX)
        ++txBytes_;
    const uint8_t expected = input_[0] == 0x68 ? uint8_t(sizeof Wake) : 5;
    if (inputSize_ == expected) {
        dispatch(now);
        inputSize_ = 0;
    }
    return true;
}
void VirtualHondaEcu::dispatch(uint32_t now) {
    if (inputSize_ == sizeof Wake && memcmp(input_, Wake, sizeof Wake) == 0) {
        initialized_ = true;
        initAt_ = now;
        return;
    }
    if (!initialized_ || uint32_t(now - initAt_) < InitWaitMs || inputSize_ != 5 || input_[0] != 0x20 ||
        input_[1] != 5 || pending())
        return;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 5; ++i)
        sum = uint8_t(sum + input_[i]);
    if (sum)
        return;
    const uint8_t *fixture = 0;
    uint8_t count = 0;
    if (input_[2] == 0 && input_[3] == 2) {
        fixture = scenario_ ? RpmB : RpmA;
        count = 5;
    } else if (input_[2] == 0x10 && input_[3] == 1) {
        fixture = scenario_ ? EctB : EctA;
        count = 4;
    } else if (input_[2] == 0x14 && input_[3] == 1) {
        fixture = scenario_ ? TpsB : TpsA;
        count = 4;
    }
    if (!fixture || fault_ == Silent)
        return;
    if (scenario_ == 3)
        fixture = input_[2] == 0 ? RpmBoundary : input_[2] == 0x10 ? EctBoundary : TpsBoundary;
    memcpy(reply_, fixture, count);
    replySize_ = count;
    replyAt_ = 0;
    replyEpoch_ = now;
    activeDelay_ = fault_ == Delay ? delay_ : 0;
    activeGap_ = fault_ == Gap ? gap_ : 0;
    switch (fault_) {
    case Header:
        reply_[0] = 0x7e;
        break;
    case Length:
        reply_[1] = 0xff;
        break;
    case Checksum:
        reply_[count - 1] ^= 1;
        break;
    case Truncated:
        --replySize_;
        break;
    case Noise:
        memmove(reply_ + 1, reply_, count);
        reply_[0] = 0x7e;
        ++replySize_;
        break;
    case Trailing:
        reply_[replySize_++] = 0x7e;
        break;
    default:
        break;
    }
}
bool VirtualHondaEcu::readByte(uint8_t &byte, uint32_t now, uint32_t &observedAt) {
    if (!pending())
        return false;
    const uint32_t due =
        uint32_t(activeDelay_) + uint32_t(replyAt_ + 1) * 2u + (replyAt_ >= 2 ? uint32_t(activeGap_) : 0u);
    if (uint32_t(now - replyEpoch_) < due)
        return false;
    observedAt = replyEpoch_ + due;
    byte = reply_[replyAt_++];
    return true;
}
} // namespace hd_bridge
