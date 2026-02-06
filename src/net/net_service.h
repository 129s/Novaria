#pragma once

#include "sim/tick_context.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace novaria::net {

enum class NetSessionState : std::uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
};

struct PlayerCommand final {
    std::uint32_t player_id = 0;
    std::string command_type;
    std::string payload;
};

struct NetDiagnosticsSnapshot final {
    NetSessionState session_state = NetSessionState::Disconnected;
    std::string last_session_transition_reason;
    std::uint64_t last_heartbeat_tick = 0;
    std::uint64_t session_transition_count = 0;
    std::uint64_t connected_transition_count = 0;
    std::uint64_t connect_request_count = 0;
    std::uint64_t connect_probe_send_count = 0;
    std::uint64_t connect_probe_send_failure_count = 0;
    std::uint64_t timeout_disconnect_count = 0;
    std::uint64_t manual_disconnect_count = 0;
    std::uint64_t ignored_heartbeat_count = 0;
    std::uint64_t ignored_unexpected_sender_count = 0;
    std::size_t dropped_command_count = 0;
    std::size_t dropped_command_disconnected_count = 0;
    std::size_t dropped_command_queue_full_count = 0;
    std::size_t dropped_remote_chunk_payload_count = 0;
    std::size_t dropped_remote_chunk_payload_disconnected_count = 0;
    std::size_t dropped_remote_chunk_payload_queue_full_count = 0;
};

class INetService {
public:
    virtual ~INetService() = default;

    virtual bool Initialize(std::string& out_error) = 0;
    virtual void Shutdown() = 0;
    virtual void RequestConnect() = 0;
    virtual void RequestDisconnect() = 0;
    virtual void NotifyHeartbeatReceived(std::uint64_t tick_index) = 0;
    virtual NetSessionState SessionState() const = 0;
    virtual NetDiagnosticsSnapshot DiagnosticsSnapshot() const = 0;
    virtual void Tick(const sim::TickContext& tick_context) = 0;
    virtual void SubmitLocalCommand(const PlayerCommand& command) = 0;
    virtual std::vector<std::string> ConsumeRemoteChunkPayloads() = 0;
    virtual void PublishWorldSnapshot(
        std::uint64_t tick_index,
        const std::vector<std::string>& encoded_dirty_chunks) = 0;
};

}  // namespace novaria::net
