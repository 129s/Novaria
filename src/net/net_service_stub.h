#pragma once

#include "net/net_service.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace novaria::net {

class NetServiceStub final : public INetService {
public:
    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void SubmitLocalCommand(const PlayerCommand& command) override;
    void PublishWorldSnapshot(std::uint64_t tick_index) override;

    std::size_t PendingCommandCount() const;
    std::size_t TotalProcessedCommandCount() const;
    std::uint64_t LastPublishedSnapshotTick() const;
    std::uint64_t SnapshotPublishCount() const;

private:
    bool initialized_ = false;
    std::vector<PlayerCommand> pending_commands_;
    std::size_t total_processed_command_count_ = 0;
    std::uint64_t last_published_snapshot_tick_ = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t snapshot_publish_count_ = 0;
};

}  // namespace novaria::net
