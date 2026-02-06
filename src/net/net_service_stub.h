#pragma once

#include "net/net_service.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace novaria::net {

class NetServiceStub final : public INetService {
public:
    static constexpr std::size_t kMaxPendingCommands = 1024;
    static constexpr std::size_t kMaxPendingRemoteChunkPayloads = 1024;

    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
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
    std::uint64_t LastPublishedSnapshotTick() const;
    std::size_t LastPublishedDirtyChunkCount() const;
    std::uint64_t SnapshotPublishCount() const;
    const std::vector<std::string>& LastPublishedEncodedChunks() const;

private:
    bool initialized_ = false;
    std::vector<PlayerCommand> pending_commands_;
    std::vector<std::string> pending_remote_chunk_payloads_;
    std::size_t total_processed_command_count_ = 0;
    std::size_t dropped_command_count_ = 0;
    std::size_t dropped_remote_chunk_payload_count_ = 0;
    std::uint64_t last_published_snapshot_tick_ = std::numeric_limits<std::uint64_t>::max();
    std::size_t last_published_dirty_chunk_count_ = 0;
    std::vector<std::string> last_published_encoded_chunks_;
    std::uint64_t snapshot_publish_count_ = 0;
};

}  // namespace novaria::net
