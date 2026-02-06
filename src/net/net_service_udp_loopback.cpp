#include "net/net_service_udp_loopback.h"

#include "core/logger.h"

#include <algorithm>
#include <string>
#include <utility>
#include <string_view>

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

constexpr std::string_view kControlPrefix = "CTRL|";
constexpr std::string_view kPayloadPrefix = "DATA|";
constexpr std::string_view kControlSyn = "SYN";
constexpr std::string_view kControlAck = "ACK";
constexpr std::string_view kControlHeartbeat = "HEARTBEAT";

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), text.begin());
}

std::string BuildControlDatagram(std::string_view control_type) {
    return std::string(kControlPrefix) + std::string(control_type);
}

std::string BuildPayloadDatagram(std::string_view payload) {
    return std::string(kPayloadPrefix) + std::string(payload);
}

}  // namespace

void NetServiceUdpLoopback::TransitionSessionState(
    NetSessionState next_state,
    std::string_view reason) {
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

bool NetServiceUdpLoopback::Initialize(std::string& out_error) {
    session_state_ = NetSessionState::Disconnected;
    pending_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    total_processed_command_count_ = 0;
    dropped_command_count_ = 0;
    dropped_remote_chunk_payload_count_ = 0;
    dropped_command_disconnected_count_ = 0;
    dropped_command_queue_full_count_ = 0;
    dropped_remote_chunk_payload_disconnected_count_ = 0;
    dropped_remote_chunk_payload_queue_full_count_ = 0;
    connect_request_count_ = 0;
    timeout_disconnect_count_ = 0;
    session_transition_count_ = 0;
    connected_transition_count_ = 0;
    manual_disconnect_count_ = 0;
    ignored_heartbeat_count_ = 0;
    last_session_transition_reason_ = "initialize";
    last_heartbeat_tick_ = kInvalidTick;
    last_published_snapshot_tick_ = std::numeric_limits<std::uint64_t>::max();
    last_published_dirty_chunk_count_ = 0;
    last_published_encoded_chunks_.clear();
    snapshot_publish_count_ = 0;
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    last_sent_heartbeat_tick_ = kInvalidTick;
    handshake_ack_received_ = false;

    if (!transport_.Open(bind_port_, out_error)) {
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
        "UDP loopback net service initialized on port " +
            std::to_string(transport_.LocalPort()) +
            ", remote=" + remote_endpoint_.host +
            ":" + std::to_string(remote_endpoint_.port) + ".");
    return true;
}

void NetServiceUdpLoopback::Shutdown() {
    if (!initialized_) {
        return;
    }

    TransitionSessionState(NetSessionState::Disconnected, "shutdown");
    pending_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    last_published_encoded_chunks_.clear();
    last_heartbeat_tick_ = kInvalidTick;
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    last_sent_heartbeat_tick_ = kInvalidTick;
    handshake_ack_received_ = false;
    transport_.Close();
    remote_endpoint_.port = remote_endpoint_config_port_;
    initialized_ = false;
    core::Logger::Info("net", "UDP loopback net service shutdown.");
}

void NetServiceUdpLoopback::RequestConnect() {
    if (!initialized_) {
        return;
    }

    if (session_state_ != NetSessionState::Disconnected) {
        return;
    }

    TransitionSessionState(NetSessionState::Connecting, "request_connect");
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    handshake_ack_received_ = false;
    ++connect_request_count_;
}

void NetServiceUdpLoopback::RequestDisconnect() {
    if (!initialized_) {
        return;
    }

    if (session_state_ == NetSessionState::Disconnected) {
        return;
    }

    ++manual_disconnect_count_;
    TransitionSessionState(NetSessionState::Disconnected, "request_disconnect");
    pending_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    last_heartbeat_tick_ = kInvalidTick;
    connect_started_tick_ = kInvalidTick;
    next_connect_probe_tick_ = kInvalidTick;
    last_sent_heartbeat_tick_ = kInvalidTick;
    handshake_ack_received_ = false;
}

void NetServiceUdpLoopback::NotifyHeartbeatReceived(std::uint64_t tick_index) {
    if (!initialized_) {
        return;
    }

    if (session_state_ != NetSessionState::Connected) {
        ++ignored_heartbeat_count_;
        return;
    }

    last_heartbeat_tick_ = tick_index;
}

NetSessionState NetServiceUdpLoopback::SessionState() const {
    return session_state_;
}

NetDiagnosticsSnapshot NetServiceUdpLoopback::DiagnosticsSnapshot() const {
    return NetDiagnosticsSnapshot{
        .session_state = session_state_,
        .last_session_transition_reason = last_session_transition_reason_,
        .last_heartbeat_tick = last_heartbeat_tick_,
        .session_transition_count = session_transition_count_,
        .connected_transition_count = connected_transition_count_,
        .connect_request_count = connect_request_count_,
        .timeout_disconnect_count = timeout_disconnect_count_,
        .manual_disconnect_count = manual_disconnect_count_,
        .ignored_heartbeat_count = ignored_heartbeat_count_,
        .dropped_command_count = dropped_command_count_,
        .dropped_command_disconnected_count = dropped_command_disconnected_count_,
        .dropped_command_queue_full_count = dropped_command_queue_full_count_,
        .dropped_remote_chunk_payload_count = dropped_remote_chunk_payload_count_,
        .dropped_remote_chunk_payload_disconnected_count =
            dropped_remote_chunk_payload_disconnected_count_,
        .dropped_remote_chunk_payload_queue_full_count =
            dropped_remote_chunk_payload_queue_full_count_,
    };
}

void NetServiceUdpLoopback::Tick(const sim::TickContext& tick_context) {
    if (!initialized_) {
        return;
    }

    if (session_state_ == NetSessionState::Connecting ||
        session_state_ == NetSessionState::Connected) {
        DrainInboundDatagrams(tick_context.tick_index);
    }

    if (session_state_ == NetSessionState::Connecting) {
        if (connect_started_tick_ == kInvalidTick) {
            connect_started_tick_ = tick_context.tick_index;
            next_connect_probe_tick_ = tick_context.tick_index;
        }

        if (next_connect_probe_tick_ == kInvalidTick ||
            tick_context.tick_index >= next_connect_probe_tick_) {
            std::string send_error;
            if (!SendControlDatagram(std::string(kControlSyn), send_error)) {
                core::Logger::Warn("net", "UDP connect probe failed: " + send_error);
            }
            next_connect_probe_tick_ = tick_context.tick_index + kConnectProbeIntervalTicks;
        }

        if (handshake_ack_received_) {
            TransitionSessionState(NetSessionState::Connected, "udp_handshake_ack");
            last_heartbeat_tick_ = tick_context.tick_index;
            last_sent_heartbeat_tick_ = tick_context.tick_index;
            handshake_ack_received_ = false;
        } else if (connect_started_tick_ != kInvalidTick &&
                   tick_context.tick_index > connect_started_tick_ + kConnectTimeoutTicks) {
            TransitionSessionState(NetSessionState::Disconnected, "connect_timeout");
            pending_commands_.clear();
            pending_remote_chunk_payloads_.clear();
            last_heartbeat_tick_ = kInvalidTick;
            connect_started_tick_ = kInvalidTick;
            next_connect_probe_tick_ = kInvalidTick;
            ++timeout_disconnect_count_;
        }
    }

    if (session_state_ == NetSessionState::Connected &&
               last_heartbeat_tick_ != kInvalidTick &&
               tick_context.tick_index > last_heartbeat_tick_ + kHeartbeatTimeoutTicks) {
        TransitionSessionState(NetSessionState::Disconnected, "heartbeat_timeout");
        pending_commands_.clear();
        pending_remote_chunk_payloads_.clear();
        last_heartbeat_tick_ = kInvalidTick;
        connect_started_tick_ = kInvalidTick;
        next_connect_probe_tick_ = kInvalidTick;
        last_sent_heartbeat_tick_ = kInvalidTick;
        ++timeout_disconnect_count_;
    }

    if (session_state_ == NetSessionState::Connected &&
        (last_sent_heartbeat_tick_ == kInvalidTick ||
            tick_context.tick_index >= last_sent_heartbeat_tick_ + kHeartbeatSendIntervalTicks)) {
        std::string heartbeat_error;
        if (!SendControlDatagram(std::string(kControlHeartbeat), heartbeat_error)) {
            core::Logger::Warn("net", "UDP heartbeat send failed: " + heartbeat_error);
        } else {
            last_sent_heartbeat_tick_ = tick_context.tick_index;
        }
    }

    total_processed_command_count_ += pending_commands_.size();
    pending_commands_.clear();
}

void NetServiceUdpLoopback::SubmitLocalCommand(const PlayerCommand& command) {
    if (!initialized_) {
        return;
    }

    if (session_state_ == NetSessionState::Disconnected) {
        ++dropped_command_count_;
        ++dropped_command_disconnected_count_;
        return;
    }

    if (pending_commands_.size() >= kMaxPendingCommands) {
        ++dropped_command_count_;
        ++dropped_command_queue_full_count_;
        return;
    }

    pending_commands_.push_back(command);
}

std::vector<std::string> NetServiceUdpLoopback::ConsumeRemoteChunkPayloads() {
    if (!initialized_) {
        return {};
    }
    if (session_state_ != NetSessionState::Connected) {
        return {};
    }

    std::vector<std::string> payloads = std::move(pending_remote_chunk_payloads_);
    pending_remote_chunk_payloads_.clear();
    return payloads;
}

void NetServiceUdpLoopback::PublishWorldSnapshot(
    std::uint64_t tick_index,
    const std::vector<std::string>& encoded_dirty_chunks) {
    if (!initialized_ || session_state_ != NetSessionState::Connected) {
        return;
    }

    last_published_snapshot_tick_ = tick_index;
    last_published_dirty_chunk_count_ = encoded_dirty_chunks.size();
    last_published_encoded_chunks_ = encoded_dirty_chunks;
    ++snapshot_publish_count_;

    std::string send_error;
    for (const auto& payload : encoded_dirty_chunks) {
        if (!SendPayloadDatagram(payload, send_error)) {
            core::Logger::Warn("net", "UDP snapshot publish failed: " + send_error);
            break;
        }
    }
}

void NetServiceUdpLoopback::SetBindPort(std::uint16_t local_port) {
    if (initialized_) {
        return;
    }

    bind_port_ = local_port;
}

void NetServiceUdpLoopback::SetRemoteEndpoint(UdpEndpoint endpoint) {
    if (endpoint.host.empty()) {
        endpoint.host = "127.0.0.1";
    }
    remote_endpoint_config_port_ = endpoint.port;
    if (initialized_ && endpoint.port == 0) {
        endpoint.port = transport_.LocalPort();
    }

    remote_endpoint_ = std::move(endpoint);
}

UdpEndpoint NetServiceUdpLoopback::RemoteEndpoint() const {
    return remote_endpoint_;
}

std::uint16_t NetServiceUdpLoopback::LocalPort() const {
    return transport_.LocalPort();
}

void NetServiceUdpLoopback::EnqueueRemoteChunkPayload(std::string payload) {
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

void NetServiceUdpLoopback::DrainInboundDatagrams(std::uint64_t tick_index) {
    std::string payload;
    UdpEndpoint sender{};
    std::string receive_error;
    while (transport_.Receive(payload, sender, receive_error)) {
        std::string_view payload_view(payload);
        if (StartsWith(payload_view, kControlPrefix)) {
            const std::string_view control_type = payload_view.substr(kControlPrefix.size());
            if (control_type == kControlSyn) {
                std::string ack_error;
                if (!SendControlDatagram(std::string(kControlAck), ack_error)) {
                    core::Logger::Warn("net", "UDP ack send failed: " + ack_error);
                }
                if (session_state_ == NetSessionState::Disconnected) {
                    TransitionSessionState(NetSessionState::Connecting, "peer_syn");
                    connect_started_tick_ = tick_index;
                    next_connect_probe_tick_ = tick_index + kConnectProbeIntervalTicks;
                }
                handshake_ack_received_ = true;
            } else if (control_type == kControlAck) {
                handshake_ack_received_ = true;
            } else if (control_type == kControlHeartbeat) {
                last_heartbeat_tick_ = tick_index;
                if (session_state_ == NetSessionState::Connecting) {
                    handshake_ack_received_ = true;
                }
            }
            payload.clear();
            continue;
        }

        if (StartsWith(payload_view, kPayloadPrefix)) {
            EnqueueRemoteChunkPayload(std::string(payload_view.substr(kPayloadPrefix.size())));
            payload.clear();
            continue;
        }

        EnqueueRemoteChunkPayload(std::move(payload));
        payload.clear();
    }

    if (!receive_error.empty()) {
        core::Logger::Warn("net", "UDP receive failed: " + receive_error);
    }
}

bool NetServiceUdpLoopback::SendControlDatagram(
    const std::string& type,
    std::string& out_error) {
    return transport_.SendTo(remote_endpoint_, BuildControlDatagram(type), out_error);
}

bool NetServiceUdpLoopback::SendPayloadDatagram(
    const std::string& payload,
    std::string& out_error) {
    return transport_.SendTo(remote_endpoint_, BuildPayloadDatagram(payload), out_error);
}

}  // namespace novaria::net
