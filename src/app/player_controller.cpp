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

    if (input_intent.hotbar_select_slot_1) {
        state_.selected_hotbar_slot = 0;
    }
    if (input_intent.hotbar_select_slot_2) {
        state_.selected_hotbar_slot = 1;
    }
    if (input_intent.hotbar_select_slot_3) {
        state_.selected_hotbar_slot = 2;
        state_.selected_place_material_id = world::WorldServiceBasic::kMaterialDirt;
    }
    if (input_intent.hotbar_select_slot_4) {
        state_.selected_hotbar_slot = 3;
        state_.selected_place_material_id = world::WorldServiceBasic::kMaterialStone;
    }

    const int target_tile_x = state_.tile_x + state_.facing_x;
    const int target_tile_y = state_.tile_y;
    if (input_intent.action_primary_held) {
        std::uint16_t target_material = 0;
        const bool has_target_material =
            world_service.TryReadTile(target_tile_x, target_tile_y, target_material);
        if (!has_target_material) {
            ResetPrimaryActionProgress();
        } else {
            constexpr int kPlaceRequiredTicks = 8;
            bool action_is_harvest = false;
            bool action_is_place = false;
            int required_ticks = 0;
            std::uint16_t place_material_id = world::WorldServiceBasic::kMaterialAir;

            if (state_.selected_hotbar_slot == 0) {
                action_is_harvest = IsPickaxeHarvestMaterial(target_material);
                if (action_is_harvest) {
                    required_ticks = RequiredHarvestTicks(target_material);
                }
            } else if (state_.selected_hotbar_slot == 1) {
                action_is_harvest = IsAxeHarvestMaterial(target_material);
                if (action_is_harvest) {
                    required_ticks = RequiredHarvestTicks(target_material);
                }
            } else if (
                state_.selected_hotbar_slot == 2 &&
                target_material == world::WorldServiceBasic::kMaterialAir &&
                state_.inventory_dirt_count > 0) {
                action_is_place = true;
                place_material_id = world::WorldServiceBasic::kMaterialDirt;
                required_ticks = kPlaceRequiredTicks;
            } else if (
                state_.selected_hotbar_slot == 3 &&
                target_material == world::WorldServiceBasic::kMaterialAir &&
                state_.inventory_stone_count > 0) {
                action_is_place = true;
                place_material_id = world::WorldServiceBasic::kMaterialStone;
                required_ticks = kPlaceRequiredTicks;
            }

            if (!action_is_harvest && !action_is_place) {
                ResetPrimaryActionProgress();
            } else {
                const bool same_progress =
                    primary_action_progress_.active &&
                    primary_action_progress_.is_harvest == action_is_harvest &&
                    primary_action_progress_.target_tile_x == target_tile_x &&
                    primary_action_progress_.target_tile_y == target_tile_y &&
                    primary_action_progress_.target_material_id == target_material &&
                    primary_action_progress_.hotbar_slot == state_.selected_hotbar_slot;
                if (!same_progress) {
                    primary_action_progress_ = PrimaryActionProgress{
                        .active = true,
                        .is_harvest = action_is_harvest,
                        .target_tile_x = target_tile_x,
                        .target_tile_y = target_tile_y,
                        .target_material_id = target_material,
                        .hotbar_slot = state_.selected_hotbar_slot,
                        .required_ticks = required_ticks,
                        .elapsed_ticks = 0,
                    };
                }

                ++primary_action_progress_.elapsed_ticks;
                if (primary_action_progress_.elapsed_ticks >= primary_action_progress_.required_ticks) {
                    if (action_is_harvest) {
                        submit_world_set_tile(
                            target_tile_x,
                            target_tile_y,
                            world::WorldServiceBasic::kMaterialAir);
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
                    } else {
                        if (place_material_id == world::WorldServiceBasic::kMaterialDirt &&
                            state_.inventory_dirt_count > 0) {
                            --state_.inventory_dirt_count;
                            submit_world_set_tile(target_tile_x, target_tile_y, place_material_id);
                        } else if (
                            place_material_id == world::WorldServiceBasic::kMaterialStone &&
                            state_.inventory_stone_count > 0) {
                            --state_.inventory_stone_count;
                            submit_world_set_tile(target_tile_x, target_tile_y, place_material_id);
                        }
                    }

                    ResetPrimaryActionProgress();
                }
            }
        }
    } else {
        ResetPrimaryActionProgress();
    }

    (void)input_intent.interaction_primary_pressed;
    (void)submit_gameplay_command;
    (void)input_intent.ui_inventory_toggle_pressed;
    (void)input_intent.hotbar_select_next_row;
    (void)input_intent.smart_mode_toggle_pressed;
    (void)input_intent.smart_context_held;
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

bool PlayerController::IsPickaxeHarvestMaterial(std::uint16_t material_id) {
    return material_id == world::WorldServiceBasic::kMaterialDirt ||
        material_id == world::WorldServiceBasic::kMaterialGrass ||
        material_id == world::WorldServiceBasic::kMaterialStone;
}

bool PlayerController::IsAxeHarvestMaterial(std::uint16_t material_id) {
    return material_id == world::WorldServiceBasic::kMaterialWood ||
        material_id == world::WorldServiceBasic::kMaterialLeaves;
}

int PlayerController::RequiredHarvestTicks(std::uint16_t material_id) {
    if (material_id == world::WorldServiceBasic::kMaterialStone) {
        return 18;
    }
    if (material_id == world::WorldServiceBasic::kMaterialWood) {
        return 12;
    }
    if (material_id == world::WorldServiceBasic::kMaterialLeaves) {
        return 6;
    }
    return 8;
}

void PlayerController::ResetPrimaryActionProgress() {
    primary_action_progress_ = {};
}

}  // namespace novaria::app
