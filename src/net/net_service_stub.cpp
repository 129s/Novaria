#include "net/net_service_stub.h"

#include "core/logger.h"

namespace novaria::net {

bool NetServiceStub::Initialize(std::string& out_error) {
    pending_commands_.clear();
    total_processed_command_count_ = 0;
    last_published_snapshot_tick_ = std::numeric_limits<std::uint64_t>::max();
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

    pending_commands_.clear();
    initialized_ = false;
    core::Logger::Info("net", "Net service stub shutdown.");
}

void NetServiceStub::Tick(const sim::TickContext& tick_context) {
    (void)tick_context;
    if (!initialized_) {
        return;
    }

    total_processed_command_count_ += pending_commands_.size();
    pending_commands_.clear();
}

void NetServiceStub::SubmitLocalCommand(const PlayerCommand& command) {
    if (!initialized_) {
        return;
    }

    pending_commands_.push_back(command);
}

void NetServiceStub::PublishWorldSnapshot(std::uint64_t tick_index) {
    if (!initialized_) {
        return;
    }

    last_published_snapshot_tick_ = tick_index;
    ++snapshot_publish_count_;
}

std::size_t NetServiceStub::PendingCommandCount() const {
    return pending_commands_.size();
}

std::size_t NetServiceStub::TotalProcessedCommandCount() const {
    return total_processed_command_count_;
}

std::uint64_t NetServiceStub::LastPublishedSnapshotTick() const {
    return last_published_snapshot_tick_;
}

std::uint64_t NetServiceStub::SnapshotPublishCount() const {
    return snapshot_publish_count_;
}

}  // namespace novaria::net
