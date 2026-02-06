#pragma once

#include "net/net_service.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::net {

class NetServiceStub final : public INetService {
public:
    static constexpr std::size_t kMaxPendingCommands = 1024;
    static constexpr std::size_t kMaxPendingRemoteChunkPayloads = 1024;
    static constexpr std::uint64_t kHeartbeatTimeoutTicks = 180;

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

    void EnqueueRemoteChunkPayload(std::string payload);
    std::size_t PendingCommandCount() const;
    std::size_t PendingRemoteChunkPayloadCount() const;
    std::size_t TotalProcessedCommandCount() const;
    std::size_t DroppedCommandCount() const;
    std::size_t DroppedRemoteChunkPayloadCount() const;
    std::uint64_t ConnectRequestCount() const;
    std::uint64_t TimeoutDisconnectCount() const;
    std::uint64_t LastHeartbeatTick() const;
    std::uint64_t SessionTransitionCount() const;
    std::uint64_t ConnectedTransitionCount() const;
    std::uint64_t ManualDisconnectCount() const;
    std::uint64_t IgnoredHeartbeatCount() const;
    std::size_t DroppedCommandDisconnectedCount() const;
    std::size_t DroppedCommandQueueFullCount() const;
    std::size_t DroppedRemoteChunkPayloadDisconnectedCount() const;
    std::size_t DroppedRemoteChunkPayloadQueueFullCount() const;
    std::uint64_t LastPublishedSnapshotTick() const;
    std::size_t LastPublishedDirtyChunkCount() const;
    std::uint64_t SnapshotPublishCount() const;
    const std::vector<std::string>& LastPublishedEncodedChunks() const;

private:
    static constexpr std::uint64_t kInvalidTick = std::numeric_limits<std::uint64_t>::max();
    void TransitionSessionState(NetSessionState next_state, std::string_view reason);

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
};

}  // namespace novaria::net
