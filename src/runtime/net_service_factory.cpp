#include "runtime/net_service_factory.h"

#include "net/net_service_udp_peer.h"

#include <utility>

namespace novaria::runtime {

std::unique_ptr<net::INetService> CreateNetService(const NetServiceConfig& config) {
    auto service = std::make_unique<net::NetServiceUdpPeer>();
    service->SetBindHost(config.local_host);
    service->SetBindPort(config.local_port);
    service->SetRemoteEndpoint(config.remote_endpoint);
    return service;
}

}  // namespace novaria::runtime

