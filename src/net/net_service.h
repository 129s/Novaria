#pragma once

#include "sim/tick_context.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace novaria::net {

struct PlayerCommand final {
    std::uint32_t player_id = 0;
    std::string command_type;
    std::string payload;
};

class INetService {
public:
    virtual ~INetService() = default;

    virtual bool Initialize(std::string& out_error) = 0;
    virtual void Shutdown() = 0;
    virtual void Tick(const sim::TickContext& tick_context) = 0;
    virtual void SubmitLocalCommand(const PlayerCommand& command) = 0;
    virtual void PublishWorldSnapshot(
        std::uint64_t tick_index,
        const std::vector<std::string>& encoded_dirty_chunks) = 0;
};

}  // namespace novaria::net
