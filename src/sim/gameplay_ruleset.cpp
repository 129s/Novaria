#include "sim/gameplay_ruleset.h"

#include "core/logger.h"
#include "sim/command_schema.h"
#include "sim/gameplay_balance.h"

#include <string>

namespace novaria::sim {
namespace {

const char* InteractionTypeName(std::uint16_t interaction_type) {
    switch (interaction_type) {
        case command::kInteractionTypeOpenCrafting:
            return "open_crafting";
        case command::kInteractionTypeCraftRecipe:
            return "craft_recipe";
        default:
            return "none";
    }
}

const char* InteractionResultName(std::uint16_t result_code) {
    switch (result_code) {
        case command::kInteractionResultSuccess:
            return "success";
        case command::kInteractionResultRejected:
            return "rejected";
        default:
            return "none";
    }
}

std::string BuildGameplayProgressPayload(
    std::string_view milestone,
    std::uint64_t tick_index) {
    std::string payload = "milestone=";
    payload += milestone;
    payload += ";tick=";
    payload += std::to_string(tick_index);
    return payload;
}

std::string BuildGameplayInteractionPayload(
    std::uint32_t player_id,
    const command::InteractionPayload& payload,
    std::uint64_t tick_index) {
    std::string output = "player=";
    output += std::to_string(player_id);
    output += ";type=";
    output += InteractionTypeName(payload.interaction_type);
    output += ";result=";
    output += InteractionResultName(payload.result_code);
    output += ";target_material_id=";
    output += std::to_string(payload.target_material_id);
    output += ";tile_x=";
    output += std::to_string(payload.target_tile_x);
    output += ";tile_y=";
    output += std::to_string(payload.target_tile_y);
    output += ";branch=";
    output += InteractionTypeName(payload.interaction_type);
    output += ";tick=";
    output += std::to_string(tick_index);
    return output;
}

std::string BuildGameplayPickupPayload(
    const GameplayPickupEvent& pickup_event,
    std::uint64_t tick_index) {
    std::string output = "player=";
    output += std::to_string(pickup_event.player_id);
    output += ";material_id=";
    output += std::to_string(pickup_event.material_id);
    output += ";amount=";
    output += std::to_string(pickup_event.amount);
    output += ";tile_x=";
    output += std::to_string(pickup_event.tile_x);
    output += ";tile_y=";
    output += std::to_string(pickup_event.tile_y);
    output += ";branch=";
    output += pickup_event.amount > 1 ? "stack" : "single";
    output += ";tick=";
    output += std::to_string(tick_index);
    return output;
}

}  // namespace

void GameplayRuleset::Reset() {
    wood_collected_ = 0;
    stone_collected_ = 0;
    workbench_built_ = false;
    sword_crafted_ = false;
    enemy_kill_count_ = 0;
    boss_health_ = balance::kBossMaxHealth;
    boss_defeated_ = false;
    playable_loop_complete_ = false;
}

GameplayProgressSnapshot GameplayRuleset::Snapshot() const {
    return GameplayProgressSnapshot{
        .wood_collected = wood_collected_,
        .stone_collected = stone_collected_,
        .workbench_built = workbench_built_,
        .sword_crafted = sword_crafted_,
        .enemy_kill_count = enemy_kill_count_,
        .boss_health = boss_health_,
        .boss_defeated = boss_defeated_,
        .playable_loop_complete = playable_loop_complete_,
    };
}

void GameplayRuleset::Restore(const GameplayProgressSnapshot& snapshot) {
    wood_collected_ = snapshot.wood_collected;
    stone_collected_ = snapshot.stone_collected;
    workbench_built_ = snapshot.workbench_built;
    sword_crafted_ = snapshot.sword_crafted;
    enemy_kill_count_ = snapshot.enemy_kill_count;
    boss_health_ = snapshot.boss_health > balance::kBossMaxHealth
        ? balance::kBossMaxHealth
        : snapshot.boss_health;
    boss_defeated_ = snapshot.boss_defeated || boss_health_ == 0;
    playable_loop_complete_ =
        snapshot.playable_loop_complete ||
        (workbench_built_ &&
            sword_crafted_ &&
            enemy_kill_count_ >= balance::kBossRequiredEnemyKills &&
            boss_defeated_);
}

void GameplayRuleset::CollectResource(
    std::uint16_t resource_id,
    std::uint32_t amount,
    std::uint64_t tick_index,
    script::IScriptHost& script_host) {
    if (amount == 0) {
        return;
    }

    if (resource_id == command::kResourceWood) {
        wood_collected_ += amount;
        DispatchGameplayProgressEvent(script_host, "collect_wood", tick_index);
        return;
    }

    if (resource_id == command::kResourceStone) {
        stone_collected_ += amount;
        DispatchGameplayProgressEvent(script_host, "collect_stone", tick_index);
    }
}

void GameplayRuleset::MarkWorkbenchBuilt(std::uint64_t tick_index, script::IScriptHost& script_host) {
    if (workbench_built_) {
        return;
    }

    workbench_built_ = true;
    DispatchGameplayProgressEvent(script_host, "build_workbench", tick_index);
    UpdatePlayableLoopCompletion(script_host, tick_index);
}

void GameplayRuleset::MarkSwordCrafted(std::uint64_t tick_index, script::IScriptHost& script_host) {
    if (sword_crafted_) {
        return;
    }

    sword_crafted_ = true;
    DispatchGameplayProgressEvent(script_host, "craft_sword", tick_index);
    UpdatePlayableLoopCompletion(script_host, tick_index);
}

void GameplayRuleset::ExecuteInteraction(
    std::uint32_t player_id,
    const command::InteractionPayload& payload,
    std::uint64_t tick_index,
    script::IScriptHost& script_host) {
    const std::string output =
        BuildGameplayInteractionPayload(player_id, payload, tick_index);
    core::Logger::Info(
        "script",
        "Dispatch gameplay interaction event: " + output);
    script_host.DispatchEvent(script::ScriptEvent{
        .event_name = "gameplay.interaction",
        .payload = output,
    });
}

void GameplayRuleset::ExecuteAttackEnemy(std::uint64_t tick_index, script::IScriptHost& script_host) {
    if (!sword_crafted_) {
        return;
    }

    ++enemy_kill_count_;
    DispatchGameplayProgressEvent(script_host, "kill_enemy", tick_index);
    UpdatePlayableLoopCompletion(script_host, tick_index);
}

void GameplayRuleset::ExecuteAttackBoss(std::uint64_t tick_index, script::IScriptHost& script_host) {
    if (!sword_crafted_ ||
        boss_defeated_ ||
        enemy_kill_count_ < balance::kBossRequiredEnemyKills) {
        return;
    }

    if (boss_health_ <= balance::kBossDamagePerAttack) {
        boss_health_ = 0;
        boss_defeated_ = true;
        DispatchGameplayProgressEvent(script_host, "defeat_boss", tick_index);
    } else {
        boss_health_ -= balance::kBossDamagePerAttack;
        DispatchGameplayProgressEvent(script_host, "attack_boss", tick_index);
    }
    UpdatePlayableLoopCompletion(script_host, tick_index);
}

void GameplayRuleset::ProcessCombatEvents(
    const std::vector<ecs::CombatEvent>& combat_events,
    std::uint64_t tick_index,
    script::IScriptHost& script_host) {
    for (const ecs::CombatEvent& combat_event : combat_events) {
        if (combat_event.type != ecs::CombatEventType::HostileDefeated ||
            combat_event.reward_kill_count == 0) {
            continue;
        }

        enemy_kill_count_ += combat_event.reward_kill_count;
        DispatchGameplayProgressEvent(script_host, "kill_enemy", tick_index);
        UpdatePlayableLoopCompletion(script_host, tick_index);
    }
}

void GameplayRuleset::ProcessGameplayEvents(
    const std::vector<ecs::GameplayEvent>& gameplay_events,
    std::uint64_t tick_index,
    script::IScriptHost& script_host,
    std::vector<GameplayPickupEvent>& out_pending_pickup_events) {
    for (const ecs::GameplayEvent& gameplay_event : gameplay_events) {
        if (gameplay_event.type != ecs::GameplayEventType::PickupResolved ||
            gameplay_event.amount == 0) {
            continue;
        }

        const GameplayPickupEvent pickup_event{
            .player_id = gameplay_event.player_id,
            .tile_x = gameplay_event.tile_x,
            .tile_y = gameplay_event.tile_y,
            .material_id = gameplay_event.material_id,
            .resource_id = gameplay_event.resource_id,
            .amount = gameplay_event.amount,
        };
        out_pending_pickup_events.push_back(pickup_event);

        const std::string payload = BuildGameplayPickupPayload(pickup_event, tick_index);
        core::Logger::Info("script", "Dispatch gameplay pickup event: " + payload);
        script_host.DispatchEvent(script::ScriptEvent{
            .event_name = "gameplay.pickup",
            .payload = payload,
        });
    }

    UpdatePlayableLoopCompletion(script_host, tick_index);
}

void GameplayRuleset::DispatchGameplayProgressEvent(
    script::IScriptHost& script_host,
    std::string_view milestone,
    std::uint64_t tick_index) const {
    script_host.DispatchEvent(script::ScriptEvent{
        .event_name = "gameplay.progress",
        .payload = BuildGameplayProgressPayload(milestone, tick_index),
    });
}

void GameplayRuleset::UpdatePlayableLoopCompletion(
    script::IScriptHost& script_host,
    std::uint64_t tick_index) {
    const bool previous_loop_complete = playable_loop_complete_;
    playable_loop_complete_ =
        workbench_built_ &&
        sword_crafted_ &&
        enemy_kill_count_ >= balance::kBossRequiredEnemyKills &&
        boss_defeated_;
    if (playable_loop_complete_ && !previous_loop_complete) {
        DispatchGameplayProgressEvent(script_host, "playable_loop_complete", tick_index);
    }
}

}  // namespace novaria::sim
