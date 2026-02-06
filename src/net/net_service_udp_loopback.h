#pragma once

#include "net/net_service.h"
#include "net/udp_transport.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::net {

class NetServiceUdpLoopback final : public INetService {
public:
    static constexpr std::size_t kMaxPendingCommands = 1024;
    static constexpr std::size_t kMaxPendingRemoteChunkPayloads = 1024;
    static constexpr std::uint64_t kHeartbeatTimeoutTicks = 180;
    static constexpr std::uint64_t kConnectProbeIntervalTicks = 30;
    static constexpr std::uint64_t kConnectTimeoutTicks = 600;
    static constexpr std::uint64_t kHeartbeatSendIntervalTicks = 30;

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

    void SetBindPort(std::uint16_t local_port);
    void SetRemoteEndpoint(UdpEndpoint endpoint);
    UdpEndpoint RemoteEndpoint() const;
    std::uint16_t LocalPort() const;

private:
    static constexpr std::uint64_t kInvalidTick = std::numeric_limits<std::uint64_t>::max();
    void TransitionSessionState(NetSessionState next_state, std::string_view reason);
    void EnqueueRemoteChunkPayload(std::string payload);
    void DrainInboundDatagrams(std::uint64_t tick_index);
    bool SendControlDatagram(const std::string& type, std::string& out_error);
    bool SendPayloadDatagram(const std::string& payload, std::string& out_error);

    bool initialized_ = false;
    NetSessionState session_state_ = NetSessionState::Disconnected;
    std::vector<PlayerCommand> pending_commands_;
    std::vector<std::string> pending_remote_chunk_payloads_;
    std::size_t total_processed_command_count_ = 0;
    std::size_t dropped_command_count_ = 0;
    std::size_t dropped_remote_chunk_payload_count_ = 0;
    std::size_t dropped_command_disconnected_count_ = 0;
    std::size_t dropped_command_queue_full_count_ = 0;
    std::size_t dropped_remote_chunk_payload_disconnected_count_ = 0;
    std::size_t dropped_remote_chunk_payload_queue_full_count_ = 0;
    std::uint64_t connect_request_count_ = 0;
    std::uint64_t timeout_disconnect_count_ = 0;
    std::uint64_t session_transition_count_ = 0;
    std::uint64_t connected_transition_count_ = 0;
    std::uint64_t manual_disconnect_count_ = 0;
    std::uint64_t ignored_heartbeat_count_ = 0;
    std::string last_session_transition_reason_ = "initialize";
    std::uint64_t last_heartbeat_tick_ = kInvalidTick;
    std::uint64_t last_published_snapshot_tick_ = std::numeric_limits<std::uint64_t>::max();
    std::size_t last_published_dirty_chunk_count_ = 0;
    std::vector<std::string> last_published_encoded_chunks_;
    std::uint64_t snapshot_publish_count_ = 0;
    std::uint16_t bind_port_ = 0;
    std::uint64_t connect_started_tick_ = kInvalidTick;
    std::uint64_t next_connect_probe_tick_ = kInvalidTick;
    std::uint64_t last_sent_heartbeat_tick_ = kInvalidTick;
    bool handshake_ack_received_ = false;
    std::uint16_t remote_endpoint_config_port_ = 0;
    UdpTransport transport_;
    UdpEndpoint remote_endpoint_{};
};

}  // namespace novaria::net
