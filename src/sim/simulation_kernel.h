#pragma once

#include "net/net_service.h"
#include "script/script_host.h"
#include "world/world_service.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::sim {

class SimulationKernel final {
public:
    static constexpr std::size_t kMaxPendingLocalCommands = 1024;

    SimulationKernel(
        world::IWorldService& world_service,
        net::INetService& net_service,
        script::IScriptHost& script_host);

    bool Initialize(std::string& out_error);
    void Shutdown();
    void SubmitLocalCommand(const net::PlayerCommand& command);
    bool ApplyRemoteChunkPayload(std::string_view encoded_payload, std::string& out_error);
    std::uint64_t CurrentTick() const;
    std::size_t PendingLocalCommandCount() const;
    std::size_t DroppedLocalCommandCount() const;
    void Update(double fixed_delta_seconds);

private:
    static bool TryParseWorldSetTileCommand(
        const net::PlayerCommand& command,
        world::TileMutation& out_mutation);
    static bool TryParseWorldChunkCommand(
        const net::PlayerCommand& command,
        std::string_view expected_command_type,
        world::ChunkCoord& out_chunk_coord);
    void ExecuteWorldCommandIfMatched(const net::PlayerCommand& command);

    bool initialized_ = false;
    std::uint64_t tick_index_ = 0;
    world::IWorldService& world_service_;
    net::INetService& net_service_;
    script::IScriptHost& script_host_;
    std::vector<net::PlayerCommand> pending_local_commands_;
    std::size_t dropped_local_command_count_ = 0;
};

}  // namespace novaria::sim
