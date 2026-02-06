#pragma once

#include "net/net_service.h"
#include "net/net_service_stub.h"
#include "net/net_service_udp_loopback.h"

#include <string>

namespace novaria::net {

enum class NetBackendPreference {
    Auto,
    Stub,
    UdpLoopback,
};

enum class NetBackendKind {
    None,
    Stub,
    UdpLoopback,
};

const char* NetBackendKindName(NetBackendKind backend_kind);
const char* NetBackendPreferenceName(NetBackendPreference preference);

class NetServiceRuntime final : public INetService {
public:
    void ConfigureUdpBackend(std::uint16_t local_port, UdpEndpoint remote_endpoint);
    void SetBackendPreference(NetBackendPreference preference);
    NetBackendPreference BackendPreference() const;
    NetBackendKind ActiveBackend() const;
    const std::string& LastBackendError() const;

    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void RequestConnect() override;
    void RequestDisconnect() override;
    void NotifyHeartbeatReceived(std::uint64_t tick_index) override;
    NetSessionState SessionState() const override;
    NetDiagnosticsSnapshot DiagnosticsSnapshot() const override;
    void Tick(const sim::TickContext& tick_context) override;
    void SubmitLocalCommand(const PlayerCommand& command) override;
    std::vector<std::string> ConsumeRemoteChunkPayloads() override;
    void PublishWorldSnapshot(
        std::uint64_t tick_index,
        const std::vector<std::string>& encoded_dirty_chunks) override;

private:
    bool InitializeWithStub(std::string& out_error);
    bool InitializeWithUdpLoopback(std::string& out_error);

    NetBackendPreference backend_preference_ = NetBackendPreference::Stub;
    NetBackendKind active_backend_ = NetBackendKind::None;
    INetService* active_host_ = nullptr;
    std::string last_backend_error_;
    std::uint16_t udp_bind_port_ = 0;
    UdpEndpoint udp_remote_endpoint_{};
    NetServiceStub stub_host_;
    NetServiceUdpLoopback udp_loopback_host_;
};

}  // namespace novaria::net
