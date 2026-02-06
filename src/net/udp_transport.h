#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace novaria::net {

struct UdpEndpoint final {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
};

class UdpTransport final {
public:
    UdpTransport();
    ~UdpTransport();

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    bool Open(std::uint16_t local_port, std::string& out_error);
    void Close();

    bool IsOpen() const;
    std::uint16_t LocalPort() const;

    bool SendTo(const UdpEndpoint& endpoint, std::string_view payload, std::string& out_error);
    bool Receive(std::string& out_payload, UdpEndpoint& out_sender, std::string& out_error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace novaria::net
