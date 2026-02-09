#include "net/net_service_udp_peer.h"

#include "core/logger.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace novaria::net {
namespace {

const char* SessionStateName(NetSessionState state) {
    switch (state) {
        case NetSessionState::Disconnected:
            return "disconnected";
        case NetSessionState::Connecting:
            return "connecting";
        case NetSessionState::Connected:
            return "connected";
    }

    return "unknown";
}

enum class ControlType : std::uint8_t {
    Syn = 1,
    Ack = 2,
    Heartbeat = 3,
};

wire::ByteBuffer BuildControlPayload(ControlType control_type) {
    wire::ByteWriter writer;
    writer.WriteU8(static_cast<wire::Byte>(control_type));
    return writer.TakeBuffer();
}

wire::ByteBuffer BuildControlDatagram(ControlType control_type) {
    wire::ByteBuffer payload = BuildControlPayload(control_type);
    wire::ByteBuffer datagram;
    wire::EncodeEnvelopeV1(
        wire::MessageKind::Control,
        wire::ByteSpan(payload.data(), payload.size()),
        datagram);
    return datagram;
}

bool TryDecodeControlPayload(wire::ByteSpan payload, ControlType& out_control_type) {
    if (payload.size() != 1) {
        return false;
    }

    out_control_type = static_cast<ControlType>(payload[0]);
    switch (out_control_type) {
        case ControlType::Syn:
        case ControlType::Ack:
        case ControlType::Heartbeat:
            return true;
    }

    return false;
}

wire::ByteBuffer BuildCommandPayload(const PlayerCommand& command) {
    wire::ByteWriter writer;
    writer.WriteVarUInt(command.player_id);
    writer.WriteVarUInt(command.command_id);
    writer.WriteBytes(wire::ByteSpan(command.payload.data(), command.payload.size()));
    return writer.TakeBuffer();
}

wire::ByteBuffer BuildCommandDatagram(const PlayerCommand& command) {
    wire::ByteBuffer payload = BuildCommandPayload(command);
    wire::ByteBuffer datagram;
    wire::EncodeEnvelopeV1(
        wire::MessageKind::Command,
        wire::ByteSpan(payload.data(), payload.size()),
        datagram);
    return datagram;
}

bool TryDecodeCommandPayload(wire::ByteSpan payload, PlayerCommand& out_command) {
    wire::ByteReader reader(payload);

    std::uint64_t player_id = 0;
    std::uint64_t command_id = 0;
    wire::ByteSpan command_payload{};
    if (!reader.ReadVarUInt(player_id) ||
        !reader.ReadVarUInt(command_id) ||
        !reader.ReadBytes(command_payload) ||
        !reader.IsFullyConsumed() ||
        player_id > std::numeric_limits<std::uint32_t>::max() ||
        command_id > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    out_command = PlayerCommand{
        .player_id = static_cast<std::uint32_t>(player_id),
        .command_id = static_cast<std::uint32_t>(command_id),
        .payload = wire::ByteBuffer(command_payload.begin(), command_payload.end()),
    };
    return true;
}

wire::ByteBuffer BuildChunkSnapshotBatchDatagram(const std::vector<wire::ByteBuffer>& chunk_snapshots) {
    wire::ByteWriter writer;
    writer.WriteVarUInt(chunk_snapshots.size());
    for (const wire::ByteBuffer& chunk : chunk_snapshots) {
        writer.WriteRawBytes(wire::ByteSpan(chunk.data(), chunk.size()));
    }

    wire::ByteBuffer datagram;
    const wire::ByteBuffer& batch_payload = writer.Buffer();
    wire::EncodeEnvelopeV1(
        wire::MessageKind::ChunkSnapshotBatch,
        wire::ByteSpan(batch_payload.data(), batch_payload.size()),
        datagram);
    return datagram;
}

bool TrySplitChunkSnapshotBatch(wire::ByteSpan payload, std::vector<wire::ByteBuffer>& out_chunks) {
    out_chunks.clear();

    wire::ByteReader reader(payload);
    std::uint64_t chunk_count = 0;
    if (!reader.ReadVarUInt(chunk_count)) {
        return false;
    }
    if (chunk_count > NetServiceUdpPeer::kMaxPendingRemoteChunkPayloads) {
        return false;
    }

    out_chunks.reserve(static_cast<std::size_t>(chunk_count));
    for (std::uint64_t i = 0; i < chunk_count; ++i) {
        const std::size_t start_offset = reader.Offset();

        std::int64_t chunk_x = 0;
        std::int64_t chunk_y = 0;
        std::uint64_t tile_count = 0;
        if (!reader.ReadVarInt(chunk_x) ||
            !reader.ReadVarInt(chunk_y) ||
            !reader.ReadVarUInt(tile_count)) {
            return false;
        }

        wire::ByteSpan tiles_bytes{};
        if (!reader.ReadBytes(tiles_bytes)) {
            return false;
        }
        if (tile_count > (std::numeric_limits<std::size_t>::max() / 2)) {
            return false;
        }
        if (tiles_bytes.size() != static_cast<std::size_t>(tile_count) * 2) {
            return false;
        }

        const std::size_t end_offset = reader.Offset();
        if (end_offset < start_offset || end_offset > payload.size()) {
            return false;
        }

        const wire::ByteSpan chunk_payload = payload.subspan(start_offset, end_offset - start_offset);
        out_chunks.emplace_back(chunk_payload.begin(), chunk_payload.end());
    }

    return reader.IsFullyConsumed();
}

std::string_view ToStringView(wire::ByteSpan bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string_view ToStringView(const wire::ByteBuffer& bytes) {
    return ToStringView(wire::ByteSpan(bytes.data(), bytes.size()));
}

wire::ByteSpan ToByteSpan(std::string_view text) {
    return wire::ByteSpan(reinterpret_cast<const wire::Byte*>(text.data()), text.size());
}

}  // namespace

void NetServiceUdpPeer::TransitionSessionState(NetSessionState next_state, std::string_view reason) {
    if (session_state_ == next_state) {
        return;
    }

    const NetSessionState previous_state = session_state_;
    session_state_ = next_state;
    last_session_transition_reason_ = std::string(reason);
    ++session_transition_count_;
    if (next_state == NetSessionState::Connected) {
        ++connected_transition_count_;
    }

    core::Logger::Info(
        "net",
        "Session transition: " +
            std::string(SessionStateName(previous_state)) +
            " -> " + std::string(SessionStateName(next_state)) +
            " (" + std::string(reason) + ").");
}

bool NetServiceUdpPeer::Initialize(std::string& out_error) {
    session_state_ = NetSessionState::Disconnected;
    pending_remote_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    total_processed_command_count_ = 0;
    dropped_command_count_ = 0;
    dropped_remote_chunk_payload_count_ = 0;
    dropped_command_disconnected_count_ = 0;
    dropped_command_queue_full_count_ = 0;
    dropped_remote_chunk_payload_disconnected_count_ = 0;
    dropped_remote_chunk_payload_queue_full_count_ = 0;
    unsent_command_count_ = 0;
    unsent_command_disconnected_count_ = 0;
    unsent_command_self_suppressed_count_ = 0;
    unsent_command_send_failure_count_ = 0;
    unsent_snapshot_payload_count_ = 0;
    unsent_snapshot_disconnected_count_ = 0;
    unsent_snapshot_self_suppressed_count_ = 0;
    unsent_snapshot_send_failure_count_ = 0;
    connect_request_count_ = 0;
    connect_probe_send_count_ = 0;
    connect_probe_send_failure_count_ = 0;
    timeout_disconnect_count_ = 0;
    session_transition_count_ = 0;
    connected_transition_count_ = 0;
    manual_disconnect_count_ = 0;
    ignored_heartbeat_count_ = 0;
    ignored_unexpected_sender_count_ = 0;
    last_session_transition_reason_ = "initialize";
    last_heartbeat_tick_ = kInvalidTick;
    last_published_snapshot_tick_ = std::numeric_limits<std::uint64_t>::max();
    last_published_dirty_chunk_count_ = 0;
    last_published_encoded_chunks_.clear();
    snapshot_publish_count_ = 0;
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    connect_probe_interval_ticks_ = kConnectProbeIntervalTicks;
    last_sent_heartbeat_tick_ = kInvalidTick;
    handshake_ack_received_ = false;

    if (!transport_.Open(bind_host_, bind_port_, out_error)) {
        initialized_ = false;
        return false;
    }

    if (remote_endpoint_.host.empty()) {
        remote_endpoint_.host = "127.0.0.1";
    }
    remote_endpoint_.port =
        remote_endpoint_config_port_ == 0 ? transport_.LocalPort() : remote_endpoint_config_port_;

    initialized_ = true;
    out_error.clear();
    core::Logger::Info(
        "net",
        "UDP peer net service initialized on " +
            bind_host_ +
            ":" +
            std::to_string(transport_.LocalPort()) +
            ", remote=" + remote_endpoint_.host +
            ":" + std::to_string(remote_endpoint_.port) + ".");
    return true;
}

void NetServiceUdpPeer::Shutdown() {
    if (!initialized_) {
        return;
    }

    TransitionSessionState(NetSessionState::Disconnected, "shutdown");
    pending_remote_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    last_published_encoded_chunks_.clear();
    last_heartbeat_tick_ = kInvalidTick;
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    connect_probe_interval_ticks_ = kConnectProbeIntervalTicks;
    last_sent_heartbeat_tick_ = kInvalidTick;
    handshake_ack_received_ = false;
    transport_.Close();
    remote_endpoint_.port = remote_endpoint_config_port_;
    initialized_ = false;
    core::Logger::Info("net", "UDP peer net service shutdown.");
}

void NetServiceUdpPeer::RequestConnect() {
    if (!initialized_) {
        return;
    }

    if (session_state_ != NetSessionState::Disconnected) {
        return;
    }

    TransitionSessionState(NetSessionState::Connecting, "request_connect");
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    connect_probe_interval_ticks_ = kConnectProbeIntervalTicks;
    handshake_ack_received_ = false;
    ++connect_request_count_;
}

void NetServiceUdpPeer::RequestDisconnect() {
    if (!initialized_) {
        return;
    }

    if (session_state_ == NetSessionState::Disconnected) {
        return;
    }

    ++manual_disconnect_count_;
    TransitionSessionState(NetSessionState::Disconnected, "request_disconnect");
    pending_remote_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    last_heartbeat_tick_ = kInvalidTick;
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    connect_probe_interval_ticks_ = kConnectProbeIntervalTicks;
    last_sent_heartbeat_tick_ = kInvalidTick;
    handshake_ack_received_ = false;
}

void NetServiceUdpPeer::NotifyHeartbeatReceived(std::uint64_t tick_index) {
    if (!initialized_) {
        return;
    }

    if (session_state_ != NetSessionState::Connected) {
        ++ignored_heartbeat_count_;
        return;
    }

    last_heartbeat_tick_ = tick_index;
}

NetSessionState NetServiceUdpPeer::SessionState() const {
    return session_state_;
}

NetDiagnosticsSnapshot NetServiceUdpPeer::DiagnosticsSnapshot() const {
    return NetDiagnosticsSnapshot{
        .session_state = session_state_,
        .last_session_transition_reason = last_session_transition_reason_,
        .last_heartbeat_tick = last_heartbeat_tick_,
        .session_transition_count = session_transition_count_,
        .connected_transition_count = connected_transition_count_,
        .connect_request_count = connect_request_count_,
        .connect_probe_send_count = connect_probe_send_count_,
        .connect_probe_send_failure_count = connect_probe_send_failure_count_,
        .timeout_disconnect_count = timeout_disconnect_count_,
        .manual_disconnect_count = manual_disconnect_count_,
        .ignored_heartbeat_count = ignored_heartbeat_count_,
        .ignored_unexpected_sender_count = ignored_unexpected_sender_count_,
        .dropped_command_count = dropped_command_count_,
        .dropped_command_disconnected_count = dropped_command_disconnected_count_,
        .dropped_command_queue_full_count = dropped_command_queue_full_count_,
        .dropped_remote_chunk_payload_count = dropped_remote_chunk_payload_count_,
        .dropped_remote_chunk_payload_disconnected_count = dropped_remote_chunk_payload_disconnected_count_,
        .dropped_remote_chunk_payload_queue_full_count = dropped_remote_chunk_payload_queue_full_count_,
        .unsent_command_count = unsent_command_count_,
        .unsent_command_disconnected_count = unsent_command_disconnected_count_,
        .unsent_command_self_suppressed_count = unsent_command_self_suppressed_count_,
        .unsent_command_send_failure_count = unsent_command_send_failure_count_,
        .unsent_snapshot_payload_count = unsent_snapshot_payload_count_,
        .unsent_snapshot_disconnected_count = unsent_snapshot_disconnected_count_,
        .unsent_snapshot_self_suppressed_count = unsent_snapshot_self_suppressed_count_,
        .unsent_snapshot_send_failure_count = unsent_snapshot_send_failure_count_,
    };
}

<<<<<<< HEAD
void NetServiceUdpPeer::Tick(const core::TickContext& tick_context) {
=======
void NetServiceUdpPeer::Tick(const sim::TickContext& tick_context) {
>>>>>>> 77c2e72a388234fbfa90639e804362c787d0e052
    if (!initialized_) {
        return;
    }

    DrainInboundDatagrams(tick_context.tick_index);

    if (session_state_ == NetSessionState::Connecting) {
        if (connect_started_tick_ == kInvalidTick) {
            connect_started_tick_ = tick_context.tick_index;
            next_connect_probe_tick_ = tick_context.tick_index;
        }

        if (next_connect_probe_tick_ == kInvalidTick ||
            tick_context.tick_index >= next_connect_probe_tick_) {
            ++connect_probe_send_count_;
            std::string send_error;
            if (!SendControlDatagram(static_cast<std::uint8_t>(ControlType::Syn), send_error)) {
                core::Logger::Warn("net", "UDP connect probe failed: " + send_error);
                ++connect_probe_send_failure_count_;
            }
            next_connect_probe_tick_ = tick_context.tick_index + connect_probe_interval_ticks_;
            connect_probe_interval_ticks_ = std::min(
                connect_probe_interval_ticks_ * 2,
                kMaxConnectProbeIntervalTicks);
        }

        if (handshake_ack_received_) {
            TransitionSessionState(NetSessionState::Connected, "udp_handshake_ack");
            last_heartbeat_tick_ = tick_context.tick_index;
            last_sent_heartbeat_tick_ = tick_context.tick_index;
            connect_probe_interval_ticks_ = kConnectProbeIntervalTicks;
            handshake_ack_received_ = false;
        } else if (connect_started_tick_ != kInvalidTick &&
                   tick_context.tick_index > connect_started_tick_ + kConnectTimeoutTicks) {
            TransitionSessionState(NetSessionState::Disconnected, "connect_timeout");
            pending_remote_commands_.clear();
            pending_remote_chunk_payloads_.clear();
            last_heartbeat_tick_ = kInvalidTick;
            connect_started_tick_ = kInvalidTick;
            next_connect_probe_tick_ = kInvalidTick;
            connect_probe_interval_ticks_ = kConnectProbeIntervalTicks;
            ++timeout_disconnect_count_;
        }
    }

    if (session_state_ == NetSessionState::Connected &&
        last_heartbeat_tick_ != kInvalidTick &&
        tick_context.tick_index > last_heartbeat_tick_ + kHeartbeatTimeoutTicks) {
        TransitionSessionState(NetSessionState::Disconnected, "heartbeat_timeout");
        pending_remote_commands_.clear();
        pending_remote_chunk_payloads_.clear();
        last_heartbeat_tick_ = kInvalidTick;
        connect_started_tick_ = kInvalidTick;
        next_connect_probe_tick_ = kInvalidTick;
        connect_probe_interval_ticks_ = kConnectProbeIntervalTicks;
        last_sent_heartbeat_tick_ = kInvalidTick;
        ++timeout_disconnect_count_;
    }

    if (session_state_ == NetSessionState::Connected &&
        (last_sent_heartbeat_tick_ == kInvalidTick ||
            tick_context.tick_index >= last_sent_heartbeat_tick_ + kHeartbeatSendIntervalTicks)) {
        std::string heartbeat_error;
        if (!SendControlDatagram(static_cast<std::uint8_t>(ControlType::Heartbeat), heartbeat_error)) {
            core::Logger::Warn("net", "UDP heartbeat send failed: " + heartbeat_error);
        } else {
            last_sent_heartbeat_tick_ = tick_context.tick_index;
        }
    }
}

void NetServiceUdpPeer::SubmitLocalCommand(const PlayerCommand& command) {
    if (!initialized_) {
        return;
    }

    if (pending_remote_commands_.size() >= kMaxPendingCommands) {
        ++dropped_command_count_;
        ++dropped_command_queue_full_count_;
        return;
    }

    pending_remote_commands_.push_back(command);

    if (session_state_ != NetSessionState::Connected) {
        ++unsent_command_count_;
        ++unsent_command_disconnected_count_;
        return;
    }

    if (IsSelfEndpoint()) {
        ++unsent_command_count_;
        ++unsent_command_self_suppressed_count_;
        return;
    }

    std::string send_error;
    const wire::ByteBuffer datagram = BuildCommandDatagram(command);
    if (!SendDatagram(datagram, send_error)) {
        ++unsent_command_count_;
        ++unsent_command_send_failure_count_;
        core::Logger::Warn("net", "UDP command send failed: " + send_error);
    }
}

std::vector<PlayerCommand> NetServiceUdpPeer::ConsumeRemoteCommands() {
    if (!initialized_) {
        return {};
    }

    std::vector<PlayerCommand> commands = std::move(pending_remote_commands_);
    pending_remote_commands_.clear();
    total_processed_command_count_ += commands.size();
    return commands;
}

std::vector<wire::ByteBuffer> NetServiceUdpPeer::ConsumeRemoteChunkPayloads() {
    if (!initialized_) {
        return {};
    }
    if (session_state_ != NetSessionState::Connected) {
        return {};
    }

    std::vector<wire::ByteBuffer> payloads = std::move(pending_remote_chunk_payloads_);
    pending_remote_chunk_payloads_.clear();
    return payloads;
}

void NetServiceUdpPeer::PublishWorldSnapshot(
    std::uint64_t tick_index,
    const std::vector<wire::ByteBuffer>& encoded_dirty_chunks) {
    if (!initialized_ || encoded_dirty_chunks.empty()) {
        return;
    }

    if (session_state_ != NetSessionState::Connected) {
        unsent_snapshot_payload_count_ += encoded_dirty_chunks.size();
        unsent_snapshot_disconnected_count_ += encoded_dirty_chunks.size();
        return;
    }

    last_published_snapshot_tick_ = tick_index;
    last_published_dirty_chunk_count_ = encoded_dirty_chunks.size();
    last_published_encoded_chunks_ = encoded_dirty_chunks;
    ++snapshot_publish_count_;

    if (IsSelfEndpoint()) {
        unsent_snapshot_payload_count_ += encoded_dirty_chunks.size();
        unsent_snapshot_self_suppressed_count_ += encoded_dirty_chunks.size();
        for (const auto& payload : encoded_dirty_chunks) {
            EnqueueRemoteChunkPayload(payload);
        }
        return;
    }

    const wire::ByteBuffer datagram = BuildChunkSnapshotBatchDatagram(encoded_dirty_chunks);
    std::string send_error;
    if (!SendDatagram(datagram, send_error)) {
        unsent_snapshot_payload_count_ += encoded_dirty_chunks.size();
        unsent_snapshot_send_failure_count_ += encoded_dirty_chunks.size();
        core::Logger::Warn("net", "UDP snapshot publish failed: " + send_error);
    }
}

void NetServiceUdpPeer::SetBindHost(std::string local_host) {
    if (initialized_) {
        return;
    }

    if (local_host.empty()) {
        local_host = "127.0.0.1";
    }

    bind_host_ = std::move(local_host);
}

void NetServiceUdpPeer::SetBindPort(std::uint16_t local_port) {
    if (initialized_) {
        return;
    }

    bind_port_ = local_port;
}

void NetServiceUdpPeer::SetRemoteEndpoint(UdpEndpoint endpoint) {
    if (endpoint.host.empty()) {
        endpoint.host = "127.0.0.1";
    }
    remote_endpoint_config_port_ = endpoint.port;
    if (initialized_ && endpoint.port == 0) {
        endpoint.port = transport_.LocalPort();
    }

    remote_endpoint_ = std::move(endpoint);
}

UdpEndpoint NetServiceUdpPeer::RemoteEndpoint() const {
    return remote_endpoint_;
}

std::uint16_t NetServiceUdpPeer::LocalPort() const {
    return transport_.LocalPort();
}

bool NetServiceUdpPeer::IsSelfEndpoint() const {
    if (!initialized_ || remote_endpoint_.port == 0) {
        return false;
    }

    if (remote_endpoint_.port != transport_.LocalPort()) {
        return false;
    }

    if (remote_endpoint_.host == "127.0.0.1" || remote_endpoint_.host == "localhost") {
        return true;
    }

    if (bind_host_ == "0.0.0.0") {
        return false;
    }

    return remote_endpoint_.host == bind_host_;
}

bool NetServiceUdpPeer::IsExpectedSender(const UdpEndpoint& sender) const {
    if (sender.port == 0 || remote_endpoint_.port == 0) {
        return false;
    }

    return sender.host == remote_endpoint_.host && sender.port == remote_endpoint_.port;
}

bool NetServiceUdpPeer::TryAdoptDynamicPeerFromSyn(const UdpEndpoint& sender) {
    if (remote_endpoint_config_port_ != 0 ||
        session_state_ == NetSessionState::Connected ||
        sender.port == 0) {
        return false;
    }

    remote_endpoint_ = sender;
    core::Logger::Info(
        "net",
        "Adopted dynamic UDP peer endpoint: " +
            remote_endpoint_.host +
            ":" + std::to_string(remote_endpoint_.port) + ".");
    return true;
}

void NetServiceUdpPeer::EnqueueRemoteCommand(PlayerCommand command) {
    if (session_state_ != NetSessionState::Connected) {
        ++dropped_command_count_;
        ++dropped_command_disconnected_count_;
        return;
    }

    if (pending_remote_commands_.size() >= kMaxPendingCommands) {
        ++dropped_command_count_;
        ++dropped_command_queue_full_count_;
        return;
    }

    pending_remote_commands_.push_back(std::move(command));
}

void NetServiceUdpPeer::EnqueueRemoteChunkPayload(wire::ByteBuffer payload) {
    if (session_state_ != NetSessionState::Connected) {
        ++dropped_remote_chunk_payload_count_;
        ++dropped_remote_chunk_payload_disconnected_count_;
        return;
    }

    if (pending_remote_chunk_payloads_.size() >= kMaxPendingRemoteChunkPayloads) {
        ++dropped_remote_chunk_payload_count_;
        ++dropped_remote_chunk_payload_queue_full_count_;
        return;
    }

    pending_remote_chunk_payloads_.push_back(std::move(payload));
}

void NetServiceUdpPeer::DrainInboundDatagrams(std::uint64_t tick_index) {
    std::string payload;
    UdpEndpoint sender{};
    std::string receive_error;
    while (transport_.Receive(payload, sender, receive_error)) {
        const wire::ByteSpan datagram_bytes = ToByteSpan(payload);

        wire::EnvelopeView envelope{};
        std::string decode_error;
        if (!wire::TryDecodeEnvelopeV1(datagram_bytes, envelope, decode_error)) {
            ++ignored_unexpected_sender_count_;
            payload.clear();
            continue;
        }

        ControlType control_type = ControlType::Heartbeat;
        const bool is_control_syn =
            envelope.kind == wire::MessageKind::Control &&
            TryDecodeControlPayload(envelope.payload, control_type) &&
            control_type == ControlType::Syn;

        if (!IsExpectedSender(sender) && !(is_control_syn && TryAdoptDynamicPeerFromSyn(sender))) {
            ++ignored_unexpected_sender_count_;
            payload.clear();
            continue;
        }

        if (envelope.kind == wire::MessageKind::Control) {
            if (!TryDecodeControlPayload(envelope.payload, control_type)) {
                ++ignored_unexpected_sender_count_;
                payload.clear();
                continue;
            }

            if (control_type == ControlType::Syn) {
                std::string ack_error;
                if (!SendControlDatagramTo(sender, static_cast<std::uint8_t>(ControlType::Ack), ack_error)) {
                    core::Logger::Warn("net", "UDP ack send failed: " + ack_error);
                }
                if (session_state_ == NetSessionState::Disconnected) {
                    TransitionSessionState(NetSessionState::Connecting, "peer_syn");
                    connect_started_tick_ = tick_index;
                    next_connect_probe_tick_ = tick_index + kConnectProbeIntervalTicks;
                }
                handshake_ack_received_ = true;
            } else if (control_type == ControlType::Ack) {
                handshake_ack_received_ = true;
            } else if (control_type == ControlType::Heartbeat) {
                last_heartbeat_tick_ = tick_index;
                if (session_state_ == NetSessionState::Connecting) {
                    handshake_ack_received_ = true;
                }
            }

            payload.clear();
            continue;
        }

        if (envelope.kind == wire::MessageKind::Command) {
            PlayerCommand command{};
            if (!TryDecodeCommandPayload(envelope.payload, command)) {
                ++dropped_command_count_;
                core::Logger::Warn("net", "UDP received invalid command datagram.");
                payload.clear();
                continue;
            }

            EnqueueRemoteCommand(std::move(command));
            payload.clear();
            continue;
        }

        if (envelope.kind == wire::MessageKind::ChunkSnapshot) {
            EnqueueRemoteChunkPayload(wire::ByteBuffer(envelope.payload.begin(), envelope.payload.end()));
            payload.clear();
            continue;
        }

        if (envelope.kind == wire::MessageKind::ChunkSnapshotBatch) {
            std::vector<wire::ByteBuffer> chunks;
            if (!TrySplitChunkSnapshotBatch(envelope.payload, chunks)) {
                ++dropped_remote_chunk_payload_count_;
                payload.clear();
                continue;
            }
            for (auto& chunk : chunks) {
                EnqueueRemoteChunkPayload(std::move(chunk));
            }
            payload.clear();
            continue;
        }

        payload.clear();
    }

    if (!receive_error.empty()) {
        core::Logger::Warn("net", "UDP receive failed: " + receive_error);
    }
}

bool NetServiceUdpPeer::SendControlDatagram(std::uint8_t control_type, std::string& out_error) {
    return SendControlDatagramTo(remote_endpoint_, control_type, out_error);
}

bool NetServiceUdpPeer::SendControlDatagramTo(
    const UdpEndpoint& endpoint,
    std::uint8_t control_type,
    std::string& out_error) {
    const wire::ByteBuffer datagram = BuildControlDatagram(static_cast<ControlType>(control_type));
    return transport_.SendTo(endpoint, ToStringView(datagram), out_error);
}

bool NetServiceUdpPeer::SendDatagram(const wire::ByteBuffer& datagram, std::string& out_error) {
    return transport_.SendTo(remote_endpoint_, ToStringView(datagram), out_error);
}

}  // namespace novaria::net
