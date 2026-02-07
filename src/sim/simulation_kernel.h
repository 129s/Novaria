#pragma once

#include "net/net_service.h"
#include "script/script_host.h"
#include "sim/ecs_runtime.h"
#include "sim/typed_command.h"
#include "world/world_service.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::sim {

struct GameplayProgressSnapshot final {
    std::uint32_t wood_collected = 0;
    std::uint32_t stone_collected = 0;
    bool workbench_built = false;
    bool sword_crafted = false;
    std::uint32_t enemy_kill_count = 0;
    std::uint32_t boss_health = 0;
    bool boss_defeated = false;
    bool playable_loop_complete = false;
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
    void SubmitLocalCommand(const net::PlayerCommand& command);
    bool ApplyRemoteChunkPayload(std::string_view encoded_payload, std::string& out_error);
    std::uint64_t CurrentTick() const;
    std::size_t PendingLocalCommandCount() const;
    std::size_t DroppedLocalCommandCount() const;
    GameplayProgressSnapshot GameplayProgress() const;
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
    void ExecuteGameplayCommandIfMatched(const TypedPlayerCommand& command);
    void ExecuteCombatCommandIfMatched(
        const TypedPlayerCommand& command,
        std::uint32_t player_id);
    void UpdatePlayableLoopCompletion();
    void DispatchGameplayProgressEvent(std::string_view milestone);
    void ResetGameplayProgress();
    void QueueNetSessionChangedEvent(
        net::NetSessionState session_state,
        std::string_view transition_reason);
    void TryDispatchPendingNetSessionEvent();

    bool initialized_ = false;
    std::uint64_t tick_index_ = 0;
    world::IWorldService& world_service_;
    net::INetService& net_service_;
    script::IScriptHost& script_host_;
    ecs::Runtime ecs_runtime_;
    std::vector<net::PlayerCommand> pending_local_commands_;
    std::size_t dropped_local_command_count_ = 0;
    net::NetSessionState last_observed_net_session_state_ = net::NetSessionState::Disconnected;
    std::uint64_t next_auto_reconnect_tick_ = 0;
    std::uint64_t next_net_session_event_dispatch_tick_ = 0;
    PendingNetSessionEvent pending_net_session_event_{};
    std::uint32_t wood_collected_ = 0;
    std::uint32_t stone_collected_ = 0;
    bool workbench_built_ = false;
    bool sword_crafted_ = false;
    std::uint32_t enemy_kill_count_ = 0;
    std::uint32_t boss_health_ = 0;
    bool boss_defeated_ = false;
    bool playable_loop_complete_ = false;
};

}  // namespace novaria::sim
