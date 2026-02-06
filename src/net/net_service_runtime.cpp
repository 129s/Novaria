#include "net/net_service_runtime.h"

#include "core/logger.h"

#include <utility>

namespace novaria::net {

const char* NetBackendKindName(NetBackendKind backend_kind) {
    switch (backend_kind) {
        case NetBackendKind::None:
            return "none";
        case NetBackendKind::Stub:
            return "stub";
        case NetBackendKind::UdpLoopback:
            return "udp_loopback";
    }

    return "unknown";
}

const char* NetBackendPreferenceName(NetBackendPreference preference) {
    switch (preference) {
        case NetBackendPreference::Auto:
            return "auto";
        case NetBackendPreference::Stub:
            return "stub";
        case NetBackendPreference::UdpLoopback:
            return "udp_loopback";
    }

    return "unknown";
}

void NetServiceRuntime::ConfigureUdpBackend(std::uint16_t local_port, UdpEndpoint remote_endpoint) {
    udp_bind_port_ = local_port;
    udp_remote_endpoint_ = std::move(remote_endpoint);
}

void NetServiceRuntime::SetBackendPreference(NetBackendPreference preference) {
    if (backend_preference_ == preference) {
        return;
    }

    Shutdown();
    backend_preference_ = preference;
}

NetBackendPreference NetServiceRuntime::BackendPreference() const {
    return backend_preference_;
}

NetBackendKind NetServiceRuntime::ActiveBackend() const {
    return active_backend_;
}

const std::string& NetServiceRuntime::LastBackendError() const {
    return last_backend_error_;
}

bool NetServiceRuntime::Initialize(std::string& out_error) {
    Shutdown();
    last_backend_error_.clear();

    if (backend_preference_ == NetBackendPreference::Stub) {
        return InitializeWithStub(out_error);
    }

    if (backend_preference_ == NetBackendPreference::UdpLoopback) {
        return InitializeWithUdpLoopback(out_error);
    }

    std::string udp_error;
    if (InitializeWithUdpLoopback(udp_error)) {
        out_error.clear();
        return true;
    }

    last_backend_error_ = udp_error;
    core::Logger::Warn(
        "net",
        "UDP loopback backend unavailable, fallback to stub backend: " + udp_error);

    if (!InitializeWithStub(out_error)) {
        return false;
    }

    out_error.clear();
    return true;
}

void NetServiceRuntime::Shutdown() {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->Shutdown();
    active_host_ = nullptr;
    active_backend_ = NetBackendKind::None;
}

void NetServiceRuntime::RequestConnect() {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->RequestConnect();
}

void NetServiceRuntime::RequestDisconnect() {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->RequestDisconnect();
}

void NetServiceRuntime::NotifyHeartbeatReceived(std::uint64_t tick_index) {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->NotifyHeartbeatReceived(tick_index);
}

NetSessionState NetServiceRuntime::SessionState() const {
    if (active_host_ == nullptr) {
        return NetSessionState::Disconnected;
    }

    return active_host_->SessionState();
}

NetDiagnosticsSnapshot NetServiceRuntime::DiagnosticsSnapshot() const {
    if (active_host_ == nullptr) {
        return {};
    }

    return active_host_->DiagnosticsSnapshot();
}

void NetServiceRuntime::Tick(const sim::TickContext& tick_context) {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->Tick(tick_context);
}

void NetServiceRuntime::SubmitLocalCommand(const PlayerCommand& command) {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->SubmitLocalCommand(command);
}

std::vector<std::string> NetServiceRuntime::ConsumeRemoteChunkPayloads() {
    if (active_host_ == nullptr) {
        return {};
    }

    return active_host_->ConsumeRemoteChunkPayloads();
}

void NetServiceRuntime::PublishWorldSnapshot(
    std::uint64_t tick_index,
    const std::vector<std::string>& encoded_dirty_chunks) {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->PublishWorldSnapshot(tick_index, encoded_dirty_chunks);
}

bool NetServiceRuntime::InitializeWithStub(std::string& out_error) {
    if (!stub_host_.Initialize(out_error)) {
        active_host_ = nullptr;
        active_backend_ = NetBackendKind::None;
        return false;
    }

    active_host_ = &stub_host_;
    active_backend_ = NetBackendKind::Stub;
    core::Logger::Info("net", "Net runtime backend: stub.");
    return true;
}

bool NetServiceRuntime::InitializeWithUdpLoopback(std::string& out_error) {
    udp_loopback_host_.SetBindPort(udp_bind_port_);
    udp_loopback_host_.SetRemoteEndpoint(udp_remote_endpoint_);

    if (!udp_loopback_host_.Initialize(out_error)) {
        active_host_ = nullptr;
        active_backend_ = NetBackendKind::None;
        return false;
    }

    active_host_ = &udp_loopback_host_;
    active_backend_ = NetBackendKind::UdpLoopback;
    core::Logger::Info("net", "Net runtime backend: udp_loopback.");
    return true;
}

}  // namespace novaria::net
