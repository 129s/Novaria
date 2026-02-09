#pragma once

#include "script/script_host.h"
#include "sim/ecs_runtime.h"
#include "sim/gameplay_types.h"
#include "sim/command_schema.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace novaria::sim {

class GameplayRuleset final {
public:
    void Reset();
    GameplayProgressSnapshot Snapshot() const;
    void Restore(const GameplayProgressSnapshot& snapshot);

    void CollectResource(
        std::uint16_t resource_id,
        std::uint32_t amount,
        std::uint64_t tick_index,
        script::IScriptHost& script_host);

    void MarkWorkbenchBuilt(std::uint64_t tick_index, script::IScriptHost& script_host);
    void MarkSwordCrafted(std::uint64_t tick_index, script::IScriptHost& script_host);

    void ExecuteInteraction(
        std::uint32_t player_id,
        const command::InteractionPayload& payload,
        std::uint64_t tick_index,
        script::IScriptHost& script_host);

    void ExecuteAttackEnemy(std::uint64_t tick_index, script::IScriptHost& script_host);
    void ExecuteAttackBoss(std::uint64_t tick_index, script::IScriptHost& script_host);

    void ProcessCombatEvents(
        const std::vector<ecs::CombatEvent>& combat_events,
        std::uint64_t tick_index,
        script::IScriptHost& script_host);

    void ProcessGameplayEvents(
        const std::vector<ecs::GameplayEvent>& gameplay_events,
        std::uint64_t tick_index,
        script::IScriptHost& script_host,
        std::vector<GameplayPickupEvent>& out_pending_pickup_events);

private:
    void DispatchGameplayProgressEvent(
        script::IScriptHost& script_host,
        std::string_view milestone,
        std::uint64_t tick_index) const;
    void UpdatePlayableLoopCompletion(
        script::IScriptHost& script_host,
        std::uint64_t tick_index);

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
