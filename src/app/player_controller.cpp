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
    std::uint32_t local_player_id) {
    constexpr std::uint32_t kWorkbenchWoodCost = 10;
    constexpr std::uint32_t kWorkbenchStoneCost = 5;
    constexpr std::uint32_t kWoodSwordWoodCost = 8;
    constexpr std::uint32_t kWoodSwordStoneCost = 12;

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

    auto submit_world_unload_chunk = [&simulation_kernel, local_player_id](int chunk_x, int chunk_y) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_type = std::string(sim::command::kWorldUnloadChunk),
            .payload = sim::command::BuildWorldChunkPayload(chunk_x, chunk_y),
        });
    };

    auto submit_gameplay_command =
        [&simulation_kernel, local_player_id](std::string_view command_type, const std::string& payload) {
            simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
                .player_id = local_player_id,
                .command_type = std::string(command_type),
                .payload = payload,
            });
        };

    auto submit_collect_resource =
        [&submit_gameplay_command](std::uint16_t resource_id, std::uint32_t amount) {
            submit_gameplay_command(
                sim::command::kGameplayCollectResource,
                sim::command::BuildCollectResourcePayload(resource_id, amount));
        };

    const world::ChunkCoord player_chunk = TileToChunkCoord(state_.tile_x, state_.tile_y);
    if (!state_.loaded_chunk_window_ready ||
        player_chunk.x != state_.loaded_chunk_window_center_x ||
        player_chunk.y != state_.loaded_chunk_window_center_y) {
        constexpr int kChunkWindowRadius = 2;
        auto is_chunk_in_window = [](int chunk_x, int chunk_y, int center_x, int center_y) {
            return chunk_x >= center_x - kChunkWindowRadius &&
                chunk_x <= center_x + kChunkWindowRadius &&
                chunk_y >= center_y - kChunkWindowRadius &&
                chunk_y <= center_y + kChunkWindowRadius;
        };

        if (state_.loaded_chunk_window_ready) {
            const int previous_center_x = state_.loaded_chunk_window_center_x;
            const int previous_center_y = state_.loaded_chunk_window_center_y;
            for (int chunk_y = previous_center_y - kChunkWindowRadius;
                 chunk_y <= previous_center_y + kChunkWindowRadius;
                 ++chunk_y) {
                for (int chunk_x = previous_center_x - kChunkWindowRadius;
                     chunk_x <= previous_center_x + kChunkWindowRadius;
                     ++chunk_x) {
                    if (is_chunk_in_window(chunk_x, chunk_y, player_chunk.x, player_chunk.y)) {
                        continue;
                    }

                    submit_world_unload_chunk(chunk_x, chunk_y);
                }
            }
        }

        for (int chunk_y = player_chunk.y - kChunkWindowRadius;
             chunk_y <= player_chunk.y + kChunkWindowRadius;
             ++chunk_y) {
            for (int chunk_x = player_chunk.x - kChunkWindowRadius;
                 chunk_x <= player_chunk.x + kChunkWindowRadius;
                 ++chunk_x) {
                if (state_.loaded_chunk_window_ready &&
                    is_chunk_in_window(
                        chunk_x,
                        chunk_y,
                        state_.loaded_chunk_window_center_x,
                        state_.loaded_chunk_window_center_y)) {
                    continue;
                }

                submit_world_load_chunk(chunk_x, chunk_y);
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
        state_.selected_place_material_id = world::WorldServiceBasic::kMaterialDirt;
    }
    if (input_intent.select_material_stone) {
        state_.selected_place_material_id = world::WorldServiceBasic::kMaterialStone;
    }

    const int target_tile_x = state_.tile_x + state_.facing_x;
    const int target_tile_y = state_.tile_y;
    if (input_intent.player_mine) {
        std::uint16_t target_material = 0;
        if (world_service.TryReadTile(target_tile_x, target_tile_y, target_material) &&
            IsCollectibleMaterial(target_material)) {
            submit_world_set_tile(target_tile_x, target_tile_y, world::WorldServiceBasic::kMaterialAir);
            if (target_material == world::WorldServiceBasic::kMaterialDirt ||
                target_material == world::WorldServiceBasic::kMaterialGrass) {
                ++state_.inventory_dirt_count;
            } else if (target_material == world::WorldServiceBasic::kMaterialStone) {
                ++state_.inventory_stone_count;
                submit_collect_resource(sim::command::kResourceStone, 1);
            } else if (target_material == world::WorldServiceBasic::kMaterialWood) {
                ++state_.inventory_wood_count;
                submit_collect_resource(sim::command::kResourceWood, 1);
            }
        }
    }

    if (input_intent.player_place) {
        std::uint16_t target_material = 0;
        const bool has_target_material =
            world_service.TryReadTile(target_tile_x, target_tile_y, target_material);
        if (!has_target_material || !IsSolidMaterial(target_material)) {
            if (state_.selected_place_material_id == world::WorldServiceBasic::kMaterialDirt &&
                state_.inventory_dirt_count > 0) {
                --state_.inventory_dirt_count;
                submit_world_set_tile(
                    target_tile_x,
                    target_tile_y,
                    world::WorldServiceBasic::kMaterialDirt);
            } else if (
                state_.selected_place_material_id == world::WorldServiceBasic::kMaterialStone &&
                state_.inventory_stone_count > 0) {
                --state_.inventory_stone_count;
                submit_world_set_tile(
                    target_tile_x,
                    target_tile_y,
                    world::WorldServiceBasic::kMaterialStone);
            }
        }
    }

    if (input_intent.build_workbench &&
        !state_.workbench_built &&
        state_.inventory_wood_count >= kWorkbenchWoodCost &&
        state_.inventory_stone_count >= kWorkbenchStoneCost) {
        state_.inventory_wood_count -= kWorkbenchWoodCost;
        state_.inventory_stone_count -= kWorkbenchStoneCost;
        state_.workbench_built = true;
        submit_gameplay_command(sim::command::kGameplayBuildWorkbench, "");
    }

    if (input_intent.craft_wood_sword &&
        !state_.wood_sword_crafted &&
        state_.workbench_built &&
        state_.inventory_wood_count >= kWoodSwordWoodCost &&
        state_.inventory_stone_count >= kWoodSwordStoneCost) {
        state_.inventory_wood_count -= kWoodSwordWoodCost;
        state_.inventory_stone_count -= kWoodSwordStoneCost;
        state_.wood_sword_crafted = true;
        submit_gameplay_command(sim::command::kGameplayCraftSword, "");
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
    return material_id != world::WorldServiceBasic::kMaterialAir &&
        material_id != world::WorldServiceBasic::kMaterialWater &&
        material_id != world::WorldServiceBasic::kMaterialLeaves;
}

bool PlayerController::IsCollectibleMaterial(std::uint16_t material_id) {
    return material_id == world::WorldServiceBasic::kMaterialDirt ||
        material_id == world::WorldServiceBasic::kMaterialGrass ||
        material_id == world::WorldServiceBasic::kMaterialStone ||
        material_id == world::WorldServiceBasic::kMaterialWood ||
        material_id == world::WorldServiceBasic::kMaterialLeaves;
}

}  // namespace novaria::app
