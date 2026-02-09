#pragma once

#include "net/net_service.h"
#include "script/script_host.h"
#include "sim/gameplay_ruleset.h"
#include "sim/gameplay_types.h"
#include "sim/ecs_runtime.h"
#include "sim/player_motion.h"
#include "sim/typed_command.h"
#include "world/world_service.h"
#include "wire/byte_io.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::sim {

enum class SimulationAuthorityMode : std::uint8_t {
    Authority = 0,
    Replica = 1,
};

class SimulationKernel final {
public:
    static constexpr std::size_t kMaxPendingLocalCommands = 1024;
    static constexpr std::uint64_t kAutoReconnectRetryIntervalTicks = 120;
    static constexpr std::uint64_t kSessionStateEventMinIntervalTicks = 15;

    SimulationKernel(
        world::IWorldService& world_service,
        net::INetService& net_service,
        script::IScriptHost& script_host);

    bool Initialize(std::string& out_error);
    void Shutdown();
    void SetLocalPlayerId(std::uint32_t player_id);
    std::uint32_t LocalPlayerId() const;
    void SetAuthorityMode(SimulationAuthorityMode authority_mode);
    SimulationAuthorityMode AuthorityMode() const;
    void SubmitLocalCommand(const net::PlayerCommand& command);
    bool ApplyRemoteChunkPayload(wire::ByteSpan encoded_payload, std::string& out_error);
    std::uint64_t CurrentTick() const;
    std::size_t PendingLocalCommandCount() const;
    std::size_t DroppedLocalCommandCount() const;
    GameplayProgressSnapshot GameplayProgress() const;
    PlayerInventorySnapshot InventorySnapshot(std::uint32_t player_id) const;
    PlayerMotionSnapshot LocalPlayerMotion() const;
    std::vector<GameplayPickupEvent> ConsumePickupEventsForPlayer(std::uint32_t player_id);
    void RestoreGameplayProgress(const GameplayProgressSnapshot& snapshot);
    void Update(double fixed_delta_seconds);

private:
    struct PendingNetSessionEvent {
        bool has_value = false;
        net::NetSessionState session_state = net::NetSessionState::Disconnected;
        std::uint64_t transition_tick = 0;
        std::string transition_reason;
    };

    void ExecuteWorldCommandIfMatched(const TypedPlayerCommand& command);
    void ExecuteControlCommandIfMatched(
        const TypedPlayerCommand& command,
        std::uint32_t player_id);
    void ExecuteGameplayCommandIfMatched(
        const TypedPlayerCommand& command,
        std::uint32_t player_id);
    void ExecuteCombatCommandIfMatched(
        const TypedPlayerCommand& command,
        std::uint32_t player_id);
    void QueueNetSessionChangedEvent(
        net::NetSessionState session_state,
        std::string_view transition_reason);
    void TryDispatchPendingNetSessionEvent();
    void QueueChunkForInitialSync(const world::ChunkCoord& chunk_coord);
    void RemoveChunkFromInitialSync(const world::ChunkCoord& chunk_coord);
    void QueueLoadedChunksForInitialSync();

    bool initialized_ = false;
    std::uint64_t tick_index_ = 0;
    std::uint32_t local_player_id_ = 1;
    world::IWorldService& world_service_;
    net::INetService& net_service_;
    script::IScriptHost& script_host_;
    ecs::Runtime ecs_runtime_;
    std::vector<net::PlayerCommand> pending_local_commands_;
    std::vector<GameplayPickupEvent> pending_pickup_events_;
    std::size_t dropped_local_command_count_ = 0;
    net::NetSessionState last_observed_net_session_state_ = net::NetSessionState::Disconnected;
    std::uint64_t next_auto_reconnect_tick_ = 0;
    std::uint64_t next_net_session_event_dispatch_tick_ = 0;
    PendingNetSessionEvent pending_net_session_event_{};
    std::vector<world::ChunkCoord> pending_initial_sync_chunks_;
    GameplayRuleset gameplay_ruleset_{};
    SimulationAuthorityMode authority_mode_ = SimulationAuthorityMode::Authority;
};

}  // namespace novaria::sim
