#include "net/net_service_stub.h"

#include "core/logger.h"

#include <utility>

namespace novaria::net {

bool NetServiceStub::Initialize(std::string& out_error) {
    session_state_ = NetSessionState::Disconnected;
    pending_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    total_processed_command_count_ = 0;
    dropped_command_count_ = 0;
    dropped_remote_chunk_payload_count_ = 0;
    connect_request_count_ = 0;
    timeout_disconnect_count_ = 0;
    last_heartbeat_tick_ = kInvalidTick;
    last_published_snapshot_tick_ = std::numeric_limits<std::uint64_t>::max();
    last_published_dirty_chunk_count_ = 0;
    last_published_encoded_chunks_.clear();
    snapshot_publish_count_ = 0;
    initialized_ = true;
    out_error.clear();
    core::Logger::Info("net", "Net service stub initialized.");
    return true;
}

void NetServiceStub::Shutdown() {
    if (!initialized_) {
        return;
    }

    session_state_ = NetSessionState::Disconnected;
    pending_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    last_published_encoded_chunks_.clear();
    last_heartbeat_tick_ = kInvalidTick;
    initialized_ = false;
    core::Logger::Info("net", "Net service stub shutdown.");
}

void NetServiceStub::RequestConnect() {
    if (!initialized_) {
        return;
    }

    if (session_state_ != NetSessionState::Disconnected) {
        return;
    }

    session_state_ = NetSessionState::Connecting;
    ++connect_request_count_;
}

void NetServiceStub::RequestDisconnect() {
    if (!initialized_) {
        return;
    }

    if (session_state_ == NetSessionState::Disconnected) {
        return;
    }

    session_state_ = NetSessionState::Disconnected;
    pending_commands_.clear();
    pending_remote_chunk_payloads_.clear();
    last_heartbeat_tick_ = kInvalidTick;
}

void NetServiceStub::NotifyHeartbeatReceived(std::uint64_t tick_index) {
    if (!initialized_) {
        return;
    }

    if (session_state_ != NetSessionState::Connected) {
        return;
    }

    last_heartbeat_tick_ = tick_index;
}

NetSessionState NetServiceStub::SessionState() const {
    return session_state_;
}

void NetServiceStub::Tick(const sim::TickContext& tick_context) {
    if (!initialized_) {
        return;
    }

    if (session_state_ == NetSessionState::Connecting) {
        session_state_ = NetSessionState::Connected;
        last_heartbeat_tick_ = tick_context.tick_index;
    } else if (session_state_ == NetSessionState::Connected &&
               last_heartbeat_tick_ != kInvalidTick &&
               tick_context.tick_index > last_heartbeat_tick_ + kHeartbeatTimeoutTicks) {
        session_state_ = NetSessionState::Disconnected;
        pending_commands_.clear();
        pending_remote_chunk_payloads_.clear();
        last_heartbeat_tick_ = kInvalidTick;
        ++timeout_disconnect_count_;
    }

    total_processed_command_count_ += pending_commands_.size();
    pending_commands_.clear();
}

void NetServiceStub::SubmitLocalCommand(const PlayerCommand& command) {
    if (!initialized_) {
        return;
    }

    if (session_state_ == NetSessionState::Disconnected) {
        ++dropped_command_count_;
        return;
    }

    if (pending_commands_.size() >= kMaxPendingCommands) {
        ++dropped_command_count_;
        return;
    }

    pending_commands_.push_back(command);
}

std::vector<std::string> NetServiceStub::ConsumeRemoteChunkPayloads() {
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

void NetServiceStub::PublishWorldSnapshot(
    std::uint64_t tick_index,
    const std::vector<std::string>& encoded_dirty_chunks) {
    if (!initialized_ || session_state_ != NetSessionState::Connected) {
        return;
    }

    last_published_snapshot_tick_ = tick_index;
    last_published_dirty_chunk_count_ = encoded_dirty_chunks.size();
    last_published_encoded_chunks_ = encoded_dirty_chunks;
    ++snapshot_publish_count_;
}

void NetServiceStub::EnqueueRemoteChunkPayload(std::string payload) {
    if (!initialized_) {
        return;
    }
    if (session_state_ != NetSessionState::Connected) {
        ++dropped_remote_chunk_payload_count_;
        return;
    }

    if (pending_remote_chunk_payloads_.size() >= kMaxPendingRemoteChunkPayloads) {
        ++dropped_remote_chunk_payload_count_;
        return;
    }

    pending_remote_chunk_payloads_.push_back(std::move(payload));
}

std::size_t NetServiceStub::PendingCommandCount() const {
    return pending_commands_.size();
}

std::size_t NetServiceStub::PendingRemoteChunkPayloadCount() const {
    return pending_remote_chunk_payloads_.size();
}

std::size_t NetServiceStub::TotalProcessedCommandCount() const {
    return total_processed_command_count_;
}

std::size_t NetServiceStub::DroppedCommandCount() const {
    return dropped_command_count_;
}

std::size_t NetServiceStub::DroppedRemoteChunkPayloadCount() const {
    return dropped_remote_chunk_payload_count_;
}

std::uint64_t NetServiceStub::ConnectRequestCount() const {
    return connect_request_count_;
}

std::uint64_t NetServiceStub::TimeoutDisconnectCount() const {
    return timeout_disconnect_count_;
}

std::uint64_t NetServiceStub::LastHeartbeatTick() const {
    return last_heartbeat_tick_;
}

std::uint64_t NetServiceStub::LastPublishedSnapshotTick() const {
    return last_published_snapshot_tick_;
}

std::size_t NetServiceStub::LastPublishedDirtyChunkCount() const {
    return last_published_dirty_chunk_count_;
}

std::uint64_t NetServiceStub::SnapshotPublishCount() const {
    return snapshot_publish_count_;
}

const std::vector<std::string>& NetServiceStub::LastPublishedEncodedChunks() const {
    return last_published_encoded_chunks_;
}

}  // namespace novaria::net
