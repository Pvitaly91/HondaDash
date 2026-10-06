#ifndef HD_BENCH_WIRE_HPP
#define HD_BENCH_WIRE_HPP
#include "../nano_dlc_bridge_lab/wire.hpp"
namespace hd_bench {
enum {
    Version = 2,
    PolicyVersion = 2,
    BackendBridge = 2,
    BackendResponder = 3,
    BridgeCapabilities = 15,
    ResponderCapabilities = 24,
    QuietMs = 10,
    Quiesce = 0x40,
    Arm = 0x41
};
static const char BridgeIdentity[] = "hondadash-dlc-bridge-bench-v1";
static const char ResponderIdentity[] = "hondadash-dlc-responder-bench-v1";
// v2 capability bits: 0 whitelist execution, 1 raw transaction Result, 2 bridge
// diagnostics, 3 low-voltage bench I/O, 4 external responder USB control.
// Outer envelope remains v1; payload version/policy are v2. HELLO_INFO byte3
// means bench_io_enabled only under this version. Vehicle connection is forbidden.
// Bridge NEW [policy, peerGenerationLE32]; responder QUIESCE [policy],
// ARM [policy, exactGenerationLE32], CONFIG [policy,scenario,fault,delayLE16,gapLE16].
// ACK is emitted for QUIESCE only after the last started response's physical stop
// bit. Deferred responses are canceled; generation advances. ARM emits no DLC bytes.
} // namespace hd_bench
#endif
