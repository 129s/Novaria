#include "runtime/save_state_loader.h"

#include "core/logger.h"
#include "world/snapshot_codec.h"

namespace novaria::runtime {

bool TryLoadSaveState(
    save::ISaveRepository& repository,
    SaveLoadResult& out_result,
    std::string& out_error) {
    out_result = {};

    save::WorldSaveState loaded_state{};
    if (!repository.LoadWorldState(loaded_state, out_error)) {
        return false;
    }

    out_result.has_state = true;
    out_result.state = loaded_state;
    out_result.local_player_id =
        loaded_state.local_player_id == 0 ? 1 : loaded_state.local_player_id;

    core::Logger::Info(
        "save",
        "Loaded world save: format=" + std::to_string(loaded_state.format_version) +
            ", tick=" + std::to_string(loaded_state.tick_index) +
            ", player=" + std::to_string(out_result.local_player_id));
    if (loaded_state.has_gameplay_snapshot) {
        core::Logger::Info(
            "save",
            "Loaded gameplay snapshot: wood=" +
                std::to_string(loaded_state.gameplay_wood_collected) +
                ", stone=" + std::to_string(loaded_state.gameplay_stone_collected) +
                ", workbench=" +
                (loaded_state.gameplay_workbench_built ? "true" : "false") +
                ", sword=" +
                (loaded_state.gameplay_sword_crafted ? "true" : "false") +
                ", enemy_kills=" +
                std::to_string(loaded_state.gameplay_enemy_kill_count) +
                ", boss_health=" + std::to_string(loaded_state.gameplay_boss_health) +
                ", boss_defeated=" +
                (loaded_state.gameplay_boss_defeated ? "true" : "false") +
                ", loop_complete=" +
                (loaded_state.gameplay_loop_complete ? "true" : "false"));
    }
    core::Logger::Info(
        "save",
        "Loaded debug net snapshot: transitions=" +
            std::to_string(loaded_state.debug_net_session_transitions) +
            ", timeout_disconnects=" +
            std::to_string(loaded_state.debug_net_timeout_disconnects) +
            ", manual_disconnects=" +
            std::to_string(loaded_state.debug_net_manual_disconnects) +
            ", last_heartbeat_tick=" +
            std::to_string(loaded_state.debug_net_last_heartbeat_tick) +
            ", dropped_commands=" +
            std::to_string(loaded_state.debug_net_dropped_commands) +
            ", dropped_payloads=" +
            std::to_string(loaded_state.debug_net_dropped_remote_payloads) +
            ", last_transition_reason=" +
            loaded_state.debug_net_last_transition_reason);

    out_error.clear();
    return true;
}

bool ApplySaveState(
    const save::WorldSaveState& loaded_state,
    sim::SimulationKernel& kernel,
    world::IWorldService& world_service) {
    (void)world_service;

    if (loaded_state.has_gameplay_snapshot) {
        kernel.RestoreGameplayProgress(sim::GameplayProgressSnapshot{
            .wood_collected = loaded_state.gameplay_wood_collected,
            .stone_collected = loaded_state.gameplay_stone_collected,
            .workbench_built = loaded_state.gameplay_workbench_built,
            .sword_crafted = loaded_state.gameplay_sword_crafted,
            .enemy_kill_count = loaded_state.gameplay_enemy_kill_count,
            .boss_health = loaded_state.gameplay_boss_health,
            .boss_defeated = loaded_state.gameplay_boss_defeated,
            .playable_loop_complete = loaded_state.gameplay_loop_complete,
        });
    }
    if (loaded_state.has_world_snapshot) {
        for (const wire::ByteBuffer& payload : loaded_state.world_chunk_payloads) {
            std::string apply_error;
            if (!kernel.ApplyRemoteChunkPayload(
                    wire::ByteSpan(payload.data(), payload.size()),
                    apply_error)) {
                core::Logger::Warn(
                    "save",
                    "Failed to apply saved chunk payload: " + apply_error);
            }
        }
    }

    return true;
}

}  // namespace novaria::runtime
