#pragma once

<<<<<<< HEAD
#include "core/tick_context.h"
=======
#include "sim/tick_context.h"
>>>>>>> 77c2e72a388234fbfa90639e804362c787d0e052
#include "wire/byte_io.h"

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
    std::uint32_t command_id = 0;
    wire::ByteBuffer payload;
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
    std::size_t unsent_command_count = 0;
    std::size_t unsent_command_disconnected_count = 0;
    std::size_t unsent_command_self_suppressed_count = 0;
    std::size_t unsent_command_send_failure_count = 0;
    std::size_t unsent_snapshot_payload_count = 0;
    std::size_t unsent_snapshot_disconnected_count = 0;
    std::size_t unsent_snapshot_self_suppressed_count = 0;
    std::size_t unsent_snapshot_send_failure_count = 0;
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
    virtual void Tick(const core::TickContext& tick_context) = 0;
    virtual void SubmitLocalCommand(const PlayerCommand& command) = 0;
    virtual std::vector<PlayerCommand> ConsumeRemoteCommands() = 0;
    virtual std::vector<wire::ByteBuffer> ConsumeRemoteChunkPayloads() = 0;
    virtual void PublishWorldSnapshot(
        std::uint64_t tick_index,
        const std::vector<wire::ByteBuffer>& encoded_dirty_chunks) = 0;
};

}  // namespace novaria::net
