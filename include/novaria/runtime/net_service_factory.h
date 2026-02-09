#pragma once

#include "net/net_service.h"
#include "net/udp_transport.h"

#include <cstdint>
#include <memory>
#include <string>

namespace novaria::runtime {

struct NetServiceConfig final {
    std::string local_host = "127.0.0.1";
    std::uint16_t local_port = 0;
    net::UdpEndpoint remote_endpoint{};
};

std::unique_ptr<net::INetService> CreateNetService(const NetServiceConfig& config);

}  // namespace novaria::runtime

