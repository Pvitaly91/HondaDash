#ifndef HD_BENCH_BRIDGE_ENDPOINT_HPP
#define HD_BENCH_BRIDGE_ENDPOINT_HPP
#include "../nano_dlc_bridge_lab/endpoint.hpp"
#include "../shared/bench_wire.hpp"
namespace hd_bench {
class BridgeEndpoint : public hd_bridge::EndpointCore {
  public:
    explicit BridgeEndpoint(hd_bridge::DlcPort &physicalPort) : EndpointCore(physicalPort, true) {}
};
} // namespace hd_bench
#endif
