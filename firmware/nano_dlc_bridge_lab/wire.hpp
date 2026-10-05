#ifndef HD_BRIDGE_WIRE_HPP
#define HD_BRIDGE_WIRE_HPP
#include <stdint.h>

// HondaDash USB envelope only. These IDs/metadata are never DLC wire fields.
namespace hd_bridge {
enum {
    Version = 1,
    PolicyVersion = 1,
    BackendVirtual = 1,
    Capabilities = 7,
    MaxPayload = 64,
    MaxFrame = 79,
    TxCapacity = 158,
    MaxDlcRx = 16,
    ResultHeader = 17,
    EventHeader = 11,
    AckSize = 7,
    InitWaitMs = 300,
    TotalTimeoutMs = 200,
    InterbyteTimeoutMs = 50,
    DlcTxTimeoutMs = 100
};
enum Command {
    Hello = 0x30,
    NewExperiment = 0x31,
    Initialize = 0x32,
    Execute = 0x33,
    Abort = 0x34,
    Configure = 0x35,
    Diagnostics = 0x36
};
enum Reply { HelloInfo = 0xb0, Ack = 0xb1, Result = 0xb2, Event = 0xb3, DiagnosticInfo = 0xb4, Error = 0xbf };
enum Status {
    Ok = 0,
    Busy = 1,
    BadCommand = 2,
    BadPayload = 3,
    NotBound = 4,
    StaleRequest = 5,
    PolicyDenied = 6,
    NeedsNewExperiment = 7,
    NotInitialized = 8,
    DlcTotalTimeout = 9,
    DlcInterbyteTimeout = 10,
    DlcHeader = 11,
    DlcLength = 12,
    DlcChecksum = 13,
    DlcTrailing = 14,
    DlcTxTimeout = 15,
    Aborted = 16,
    Overflow = 17,
    DlcUnexpected = 18,
    IdConflict = 19
};
enum State { NeedsExperiment = 0, ReadyForInit = 1, Initializing = 2, Ready = 3, Executing = 4, Faulted = 5 };
enum Fault {
    NoFault = 0,
    Silent = 1,
    Delay = 2,
    Gap = 3,
    Header = 4,
    Length = 5,
    Checksum = 6,
    Truncated = 7,
    Noise = 8,
    Trailing = 9
};
static const char Identity[] = "hondadash-dlc-bridge-lab-v1";
inline uint16_t load16(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t load32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void store16(uint8_t *p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
inline void store32(uint8_t *p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}
// Requests: HELLO [version,policy]; NEW/INIT/ABORT/DIAG [policy];
// EXECUTE [policy,expectedReplySize,exact DLC request (5)];
// CONFIG [policy,scenario(0=A/1=B/3=Boundary),fault,delayLE16,gapLE16].
// HELLO_INFO: version,policy,backend,physical,capsLE16,generationLE32,state,
// firmwareMajor,minor,patch,identityLength,ASCII identity (no NUL).
// ACK/ERROR: version,generationLE32,command,status.
// RESULT: version,generationLE32,status,command,address,readLength,txLength,rxLength,
// txElapsedLE16,responseElapsedLE16,maxGapLE16,actualTX[],actualRX[].
// EVENT: version,generationLE32,sequenceLE16,status,elapsedLE16,rxLength,rawRX[].
// Event elapsed is time since experiment boundary, saturating uint16, not a clock sync.
// DIAG: version,generationLE32,state,activeCommand,pendingDlcRx,pendingUsbLE16,
// dlcTxBytesLE32,parserErrorsLE32,txOverflowsLE32,lostEventsLE32.
} // namespace hd_bridge
#endif
