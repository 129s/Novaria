#include "app/player_controller.h"

#include "sim/command_schema.h"

namespace novaria::app {

void PlayerController::Reset() {
    state_ = {};
}

const LocalPlayerState& PlayerController::State() const {
    return state_;
}

void PlayerController::Update(
    const PlayerInputIntent& input_intent,
    world::WorldServiceBasic& world_service,
    sim::SimulationKernel& simulation_kernel,
    script::IScriptHost& script_host,
    std::uint32_t local_player_id,
    std::uint64_t& io_script_ping_counter) {
    auto submit_world_set_tile =
        [&simulation_kernel, local_player_id](int tile_x, int tile_y, std::uint16_t material_id) {
            simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
                .player_id = local_player_id,
                .command_type = std::string(sim::command::kWorldSetTile),
                .payload = sim::command::BuildWorldSetTilePayload(
                    tile_x,
                    tile_y,
                    material_id),
            });
        };

    auto submit_world_load_chunk = [&simulation_kernel, local_player_id](int chunk_x, int chunk_y) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kWorldLoadChunk),
            .payload = sim::command::BuildWorldChunkPayload(chunk_x, chunk_y),
        });
    };

    const world::ChunkCoord player_chunk = TileToChunkCoord(state_.tile_x, state_.tile_y);
    if (!state_.loaded_chunk_window_ready ||
        player_chunk.x != state_.loaded_chunk_window_center_x ||
        player_chunk.y != state_.loaded_chunk_window_center_y) {
        constexpr int kChunkWindowRadius = 2;
        for (int offset_y = -kChunkWindowRadius;
             offset_y <= kChunkWindowRadius;
             ++offset_y) {
            for (int offset_x = -kChunkWindowRadius;
                 offset_x <= kChunkWindowRadius;
                 ++offset_x) {
                submit_world_load_chunk(
                    player_chunk.x + offset_x,
                    player_chunk.y + offset_y);
            }
        }

        state_.loaded_chunk_window_ready = true;
        state_.loaded_chunk_window_center_x = player_chunk.x;
        state_.loaded_chunk_window_center_y = player_chunk.y;
    }

    auto try_move_player = [this, &world_service](int delta_x, int delta_y) {
        const int next_x = state_.tile_x + delta_x;
        const int next_y = state_.tile_y + delta_y;
        std::uint16_t destination_material = 0;
        if (world_service.TryReadTile(next_x, next_y, destination_material) &&
            IsSolidMaterial(destination_material)) {
            return;
        }

        state_.tile_x = next_x;
        state_.tile_y = next_y;
    };

    if (input_intent.move_left) {
        state_.facing_x = -1;
        try_move_player(-1, 0);
    }
    if (input_intent.move_right) {
        state_.facing_x = 1;
        try_move_player(1, 0);
    }
    if (input_intent.move_up) {
        try_move_player(0, -1);
    }
    if (input_intent.move_down) {
        try_move_player(0, 1);
    }

    if (input_intent.select_material_dirt) {
        state_.selected_place_material_id = 1;
    }
    if (input_intent.select_material_stone) {
        state_.selected_place_material_id = 2;
    }

    const int target_tile_x = state_.tile_x + state_.facing_x;
    const int target_tile_y = state_.tile_y;
    if (input_intent.player_mine) {
        std::uint16_t target_material = 0;
        if (world_service.TryReadTile(target_tile_x, target_tile_y, target_material) &&
            IsCollectibleMaterial(target_material)) {
            submit_world_set_tile(target_tile_x, target_tile_y, 0);
            if (target_material == 1) {
                ++state_.inventory_dirt_count;
            } else if (target_material == 2) {
                ++state_.inventory_stone_count;
            }
        }
    }

    if (input_intent.player_place) {
        std::uint16_t target_material = 0;
        const bool has_target_material =
            world_service.TryReadTile(target_tile_x, target_tile_y, target_material);
        if (!has_target_material || !IsSolidMaterial(target_material)) {
            if (state_.selected_place_material_id == 1 && state_.inventory_dirt_count > 0) {
                --state_.inventory_dirt_count;
                submit_world_set_tile(target_tile_x, target_tile_y, 1);
            } else if (
                state_.selected_place_material_id == 2 && state_.inventory_stone_count > 0) {
                --state_.inventory_stone_count;
                submit_world_set_tile(target_tile_x, target_tile_y, 2);
            }
        }
    }

    if (input_intent.send_jump_command) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kJump),
            .payload = "",
        });
    }

    if (input_intent.fire_projectile) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kCombatFireProjectile),
            .payload = sim::command::BuildFireProjectilePayload(
                state_.tile_x + state_.facing_x,
                state_.tile_y,
                state_.facing_x * 4500,
                0,
                13,
                180,
                1),
        });
    }

    if (input_intent.emit_script_ping) {
        script_host.DispatchEvent(script::ScriptEvent{
            .event_name = "debug.ping",
            .payload = std::to_string(io_script_ping_counter++),
        });
    }

    if (input_intent.debug_set_tile_air) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kWorldSetTile),
            .payload = sim::command::BuildWorldSetTilePayload(0, 0, 0),
        });
    }

    if (input_intent.debug_set_tile_stone) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kWorldSetTile),
            .payload = sim::command::BuildWorldSetTilePayload(0, 0, 2),
        });
    }

    if (input_intent.gameplay_collect_wood) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kGameplayCollectResource),
            .payload = sim::command::BuildCollectResourcePayload(
                sim::command::kResourceWood,
                5),
        });
    }

    if (input_intent.gameplay_collect_stone) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kGameplayCollectResource),
            .payload = sim::command::BuildCollectResourcePayload(
                sim::command::kResourceStone,
                5),
        });
    }

    if (input_intent.gameplay_build_workbench) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kGameplayBuildWorkbench),
            .payload = "",
        });
    }

    if (input_intent.gameplay_craft_sword) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kGameplayCraftSword),
            .payload = "",
        });
    }

    if (input_intent.gameplay_attack_enemy) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kGameplayAttackEnemy),
            .payload = "",
        });
    }

    if (input_intent.gameplay_attack_boss) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kGameplayAttackBoss),
            .payload = "",
        });
    }
}

int PlayerController::FloorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

world::ChunkCoord PlayerController::TileToChunkCoord(int tile_x, int tile_y) {
    return world::ChunkCoord{
        .x = FloorDiv(tile_x, world::WorldServiceBasic::kChunkSize),
        .y = FloorDiv(tile_y, world::WorldServiceBasic::kChunkSize),
    };
}

bool PlayerController::IsSolidMaterial(std::uint16_t material_id) {
    return material_id != 0;
}

bool PlayerController::IsCollectibleMaterial(std::uint16_t material_id) {
    return material_id == 1 || material_id == 2;
}

}  // namespace novaria::app
