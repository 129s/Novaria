#include "net/udp_transport.h"

#include <array>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace novaria::net {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

void CloseNativeSocket(NativeSocket socket_handle) {
#if defined(_WIN32)
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

#if defined(_WIN32)
bool AcquireSocketSubsystem(std::string& out_error) {
    WSADATA wsa_data{};
    const int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        out_error = "WSAStartup failed, code=" + std::to_string(startup_result);
        return false;
    }

    out_error.clear();
    return true;
}

void ReleaseSocketSubsystem() {
    WSACleanup();
}
#endif

bool SetNonBlocking(NativeSocket socket_handle, std::string& out_error) {
#if defined(_WIN32)
    u_long mode = 1;
    if (ioctlsocket(socket_handle, FIONBIO, &mode) != 0) {
        out_error = "ioctlsocket(FIONBIO) failed, code=" + std::to_string(WSAGetLastError());
        return false;
    }
#else
    const int flags = fcntl(socket_handle, F_GETFL, 0);
    if (flags < 0) {
        out_error = "fcntl(F_GETFL) failed: " + std::string(std::strerror(errno));
        return false;
    }

    if (fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) < 0) {
        out_error = "fcntl(F_SETFL) failed: " + std::string(std::strerror(errno));
        return false;
    }
#endif

    out_error.clear();
    return true;
}

bool BuildEndpointAddress(const UdpEndpoint& endpoint, sockaddr_in& out_address, std::string& out_error) {
    if (endpoint.port == 0) {
        out_error = "endpoint port must be non-zero";
        return false;
    }

    std::memset(&out_address, 0, sizeof(out_address));
    out_address.sin_family = AF_INET;
    out_address.sin_port = htons(endpoint.port);
    const int parse_result = inet_pton(AF_INET, endpoint.host.c_str(), &out_address.sin_addr);
    if (parse_result != 1) {
        out_error = "invalid IPv4 host: " + endpoint.host;
        return false;
    }

    out_error.clear();
    return true;
}

bool BuildBindAddress(
    std::string_view local_host,
    std::uint16_t local_port,
    sockaddr_in& out_address,
    std::string& out_error) {
    std::memset(&out_address, 0, sizeof(out_address));
    out_address.sin_family = AF_INET;
    out_address.sin_port = htons(local_port);

    if (local_host == "0.0.0.0") {
        out_address.sin_addr.s_addr = htonl(INADDR_ANY);
        out_error.clear();
        return true;
    }

    std::string host_text(local_host);
    if (host_text.empty()) {
        host_text = "127.0.0.1";
    }

    const int parse_result = inet_pton(AF_INET, host_text.c_str(), &out_address.sin_addr);
    if (parse_result != 1) {
        out_error = "invalid bind IPv4 host: " + host_text;
        return false;
    }

    out_error.clear();
    return true;
}

std::string AddressToString(const sockaddr_in& address) {
    char output_buffer[INET_ADDRSTRLEN] = {};
    const char* text = inet_ntop(AF_INET, &address.sin_addr, output_buffer, INET_ADDRSTRLEN);
    if (text == nullptr) {
        return "0.0.0.0";
    }
    return std::string(text);
}

bool IsWouldBlockError(int error_code) {
#if defined(_WIN32)
    return error_code == WSAEWOULDBLOCK || error_code == WSAECONNRESET;
#else
    (void)error_code;
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

std::string BuildSocketErrorMessage(const char* prefix, int error_code = 0) {
#if defined(_WIN32)
    if (error_code == 0) {
        error_code = WSAGetLastError();
    }
    return std::string(prefix) + ", code=" + std::to_string(error_code);
#else
    (void)error_code;
    return std::string(prefix) + ": " + std::string(std::strerror(errno));
#endif
}

}  // namespace

struct UdpTransport::Impl final {
    NativeSocket socket_handle = kInvalidSocket;
    std::uint16_t local_port = 0;
#if defined(_WIN32)
    bool socket_subsystem_acquired = false;
#endif
};

UdpTransport::UdpTransport()
    : impl_(std::make_unique<Impl>()) {}

UdpTransport::~UdpTransport() {
    Close();
}

bool UdpTransport::Open(std::string_view local_host, std::uint16_t local_port, std::string& out_error) {
    Close();

#if defined(_WIN32)
    if (!AcquireSocketSubsystem(out_error)) {
        return false;
    }
    impl_->socket_subsystem_acquired = true;
#endif

    NativeSocket socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        out_error = BuildSocketErrorMessage("socket creation failed");
#if defined(_WIN32)
        ReleaseSocketSubsystem();
        impl_->socket_subsystem_acquired = false;
#endif
        return false;
    }

    if (!SetNonBlocking(socket_handle, out_error)) {
        CloseNativeSocket(socket_handle);
#if defined(_WIN32)
        ReleaseSocketSubsystem();
        impl_->socket_subsystem_acquired = false;
#endif
        return false;
    }

    sockaddr_in bind_address{};
    if (!BuildBindAddress(local_host, local_port, bind_address, out_error)) {
        CloseNativeSocket(socket_handle);
#if defined(_WIN32)
        ReleaseSocketSubsystem();
        impl_->socket_subsystem_acquired = false;
#endif
        return false;
    }
    if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
        out_error = BuildSocketErrorMessage("bind failed");
        CloseNativeSocket(socket_handle);
#if defined(_WIN32)
        ReleaseSocketSubsystem();
        impl_->socket_subsystem_acquired = false;
#endif
        return false;
    }

    sockaddr_in actual_address{};
#if defined(_WIN32)
    int actual_address_size = sizeof(actual_address);
#else
    socklen_t actual_address_size = sizeof(actual_address);
#endif
    if (getsockname(
            socket_handle,
            reinterpret_cast<sockaddr*>(&actual_address),
            &actual_address_size) != 0) {
        out_error = BuildSocketErrorMessage("getsockname failed");
        CloseNativeSocket(socket_handle);
#if defined(_WIN32)
        ReleaseSocketSubsystem();
        impl_->socket_subsystem_acquired = false;
#endif
        return false;
    }

    impl_->socket_handle = socket_handle;
    impl_->local_port = ntohs(actual_address.sin_port);
    out_error.clear();
    return true;
}

bool UdpTransport::Open(std::uint16_t local_port, std::string& out_error) {
    return Open("127.0.0.1", local_port, out_error);
}

void UdpTransport::Close() {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->socket_handle != kInvalidSocket) {
        CloseNativeSocket(impl_->socket_handle);
        impl_->socket_handle = kInvalidSocket;
    }

#if defined(_WIN32)
    if (impl_->socket_subsystem_acquired) {
        ReleaseSocketSubsystem();
        impl_->socket_subsystem_acquired = false;
    }
#endif

    impl_->local_port = 0;
}

bool UdpTransport::IsOpen() const {
    return impl_ != nullptr && impl_->socket_handle != kInvalidSocket;
}

std::uint16_t UdpTransport::LocalPort() const {
    return impl_ != nullptr ? impl_->local_port : 0;
}

bool UdpTransport::SendTo(
    const UdpEndpoint& endpoint,
    std::string_view payload,
    std::string& out_error) {
    if (!IsOpen()) {
        out_error = "transport is not open";
        return false;
    }

    sockaddr_in endpoint_address{};
    if (!BuildEndpointAddress(endpoint, endpoint_address, out_error)) {
        return false;
    }

    const int send_result = sendto(
        impl_->socket_handle,
        payload.data(),
        static_cast<int>(payload.size()),
        0,
        reinterpret_cast<const sockaddr*>(&endpoint_address),
        sizeof(endpoint_address));
    if (send_result < 0) {
        out_error = BuildSocketErrorMessage("sendto failed");
        return false;
    }

    if (static_cast<std::size_t>(send_result) != payload.size()) {
        out_error = "sendto failed: partial datagram write";
        return false;
    }

    out_error.clear();
    return true;
}

bool UdpTransport::Receive(std::string& out_payload, UdpEndpoint& out_sender, std::string& out_error) {
    if (!IsOpen()) {
        out_error = "transport is not open";
        return false;
    }

    std::array<char, 65535> receive_buffer{};
    sockaddr_in sender_address{};
#if defined(_WIN32)
    int sender_address_size = sizeof(sender_address);
#else
    socklen_t sender_address_size = sizeof(sender_address);
#endif
    const int receive_result = recvfrom(
        impl_->socket_handle,
        receive_buffer.data(),
        static_cast<int>(receive_buffer.size()),
        0,
        reinterpret_cast<sockaddr*>(&sender_address),
        &sender_address_size);
    if (receive_result < 0) {
#if defined(_WIN32)
        const int socket_error = WSAGetLastError();
#else
        const int socket_error = errno;
#endif
        if (IsWouldBlockError(socket_error)) {
            out_error.clear();
            return false;
        }

        out_error = BuildSocketErrorMessage("recvfrom failed", socket_error);
        return false;
    }

    out_payload.assign(receive_buffer.data(), static_cast<std::size_t>(receive_result));
    out_sender.host = AddressToString(sender_address);
    out_sender.port = ntohs(sender_address.sin_port);
    out_error.clear();
    return true;
}

}  // namespace novaria::net
