#include "app/player_controller.h"

#include "sim/command_schema.h"

#include <algorithm>
#include <cstddef>

namespace novaria::app {

void PlayerController::Reset() {
    state_ = {};
    primary_action_progress_ = {};
    world_drops_.clear();
}

const LocalPlayerState& PlayerController::State() const {
    return state_;
}

void PlayerController::Update(
    const PlayerInputIntent& input_intent,
    world::WorldServiceBasic& world_service,
    sim::SimulationKernel& simulation_kernel,
    std::uint32_t local_player_id) {
    constexpr int kTilePixelSize = 32;
    constexpr int kReachDistanceTiles = 4;
    constexpr std::uint8_t kHotbarRows = 2;
    constexpr std::uint32_t kWorkbenchWoodCost = 10;
    constexpr std::uint32_t kWoodSwordWoodCost = 7;

    if (state_.pickup_toast_ticks_remaining > 0) {
        --state_.pickup_toast_ticks_remaining;
        if (state_.pickup_toast_ticks_remaining == 0) {
            state_.pickup_toast_material_id = 0;
            state_.pickup_toast_amount = 0;
        }
    }

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

    auto can_move_to = [&world_service](int tile_x, int tile_y) {
        std::uint16_t destination_material = 0;
        if (!world_service.TryReadTile(tile_x, tile_y, destination_material)) {
            return false;
        }

        return !IsSolidMaterial(destination_material);
    };

    if (input_intent.move_left) {
        state_.facing_x = -1;
        if (can_move_to(state_.tile_x - 1, state_.tile_y)) {
            --state_.tile_x;
        }
    }
    if (input_intent.move_right) {
        state_.facing_x = 1;
        if (can_move_to(state_.tile_x + 1, state_.tile_y)) {
            ++state_.tile_x;
        }
    }

    auto is_on_ground = [&can_move_to, this]() {
        return !can_move_to(state_.tile_x, state_.tile_y + 1);
    };

    if (input_intent.jump_pressed && is_on_ground()) {
        state_.jump_ticks_remaining = 4;
    }

    if (state_.jump_ticks_remaining > 0) {
        if (can_move_to(state_.tile_x, state_.tile_y - 1)) {
            --state_.tile_y;
            --state_.jump_ticks_remaining;
        } else {
            state_.jump_ticks_remaining = 0;
        }
    } else if (can_move_to(state_.tile_x, state_.tile_y + 1)) {
        ++state_.tile_y;
    }

    auto apply_hotbar_slot = [this](std::uint8_t slot_index) {
        state_.selected_hotbar_slot = slot_index;
        if (slot_index == 2) {
            state_.selected_place_material_id = world::WorldServiceBasic::kMaterialDirt;
        } else if (slot_index == 3) {
            state_.selected_place_material_id = world::WorldServiceBasic::kMaterialStone;
        } else if (slot_index == 4) {
            state_.selected_place_material_id = world::WorldServiceBasic::kMaterialTorch;
        } else if (slot_index == 5) {
            state_.selected_place_material_id = world::WorldServiceBasic::kMaterialWorkbench;
        }
    };

    if (input_intent.ui_inventory_toggle_pressed) {
        state_.inventory_open = !state_.inventory_open;
    }

    if (state_.inventory_open) {
        if (input_intent.hotbar_select_slot_1) {
            state_.selected_recipe_index = 0;
        } else if (input_intent.hotbar_select_slot_2) {
            state_.selected_recipe_index = 1;
        } else if (input_intent.hotbar_select_slot_3) {
            state_.selected_recipe_index = 2;
        }
    } else {
        if (input_intent.hotbar_select_slot_1) {
            apply_hotbar_slot(0);
        }
        if (input_intent.hotbar_select_slot_2) {
            apply_hotbar_slot(1);
        }
        if (input_intent.hotbar_select_slot_3) {
            apply_hotbar_slot(2);
        }
        if (input_intent.hotbar_select_slot_4) {
            apply_hotbar_slot(3);
        }
        if (input_intent.hotbar_select_slot_5) {
            apply_hotbar_slot(4);
        }
        if (input_intent.hotbar_select_slot_6) {
            apply_hotbar_slot(5);
        }
        if (input_intent.hotbar_select_slot_7) {
            apply_hotbar_slot(6);
        }
        if (input_intent.hotbar_select_slot_8) {
            apply_hotbar_slot(7);
        }
        if (input_intent.hotbar_select_slot_9) {
            apply_hotbar_slot(8);
        }
        if (input_intent.hotbar_select_slot_10) {
            apply_hotbar_slot(9);
        }
        if (input_intent.hotbar_cycle_prev) {
            apply_hotbar_slot(static_cast<std::uint8_t>((state_.selected_hotbar_slot + 9) % 10));
        } else if (input_intent.hotbar_cycle_next) {
            apply_hotbar_slot(static_cast<std::uint8_t>((state_.selected_hotbar_slot + 1) % 10));
        }
        if (input_intent.hotbar_select_next_row) {
            state_.active_hotbar_row =
                static_cast<std::uint8_t>((state_.active_hotbar_row + 1) % kHotbarRows);
        }
    }

    int target_tile_x = state_.tile_x + state_.facing_x;
    int target_tile_y = state_.tile_y;
    if (input_intent.cursor_valid &&
        input_intent.viewport_width > 0 &&
        input_intent.viewport_height > 0) {
        const int view_tiles_x = std::max(1, input_intent.viewport_width / kTilePixelSize);
        const int view_tiles_y = std::max(1, input_intent.viewport_height / kTilePixelSize);
        const int first_world_tile_x = state_.tile_x - view_tiles_x / 2;
        const int first_world_tile_y = state_.tile_y - view_tiles_y / 2;
        target_tile_x = first_world_tile_x + (input_intent.cursor_screen_x / kTilePixelSize);
        target_tile_y = first_world_tile_y + (input_intent.cursor_screen_y / kTilePixelSize);
    }

    if (target_tile_x < state_.tile_x) {
        state_.facing_x = -1;
    } else if (target_tile_x > state_.tile_x) {
        state_.facing_x = 1;
    }

    const int delta_x = target_tile_x - state_.tile_x;
    const int delta_y = target_tile_y - state_.tile_y;
    const int distance_squared = delta_x * delta_x + delta_y * delta_y;
    const bool target_reachable = distance_squared <= (kReachDistanceTiles * kReachDistanceTiles);

    if (!state_.inventory_open && input_intent.action_primary_held && target_reachable) {
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

            if (state_.active_hotbar_row == 0 && state_.selected_hotbar_slot == 0) {
                action_is_harvest = IsPickaxeHarvestMaterial(target_material);
                if (action_is_harvest) {
                    required_ticks = RequiredHarvestTicks(target_material);
                }
            } else if (state_.active_hotbar_row == 0 && state_.selected_hotbar_slot == 1) {
                action_is_harvest = IsAxeHarvestMaterial(target_material);
                if (action_is_harvest) {
                    required_ticks = RequiredHarvestTicks(target_material);
                }
            } else if (
                state_.active_hotbar_row == 0 &&
                state_.selected_hotbar_slot == 2 &&
                target_material == world::WorldServiceBasic::kMaterialAir &&
                state_.inventory_dirt_count > 0) {
                action_is_place = true;
                place_material_id = world::WorldServiceBasic::kMaterialDirt;
                required_ticks = kPlaceRequiredTicks;
            } else if (
                state_.active_hotbar_row == 0 &&
                state_.selected_hotbar_slot == 3 &&
                target_material == world::WorldServiceBasic::kMaterialAir &&
                state_.inventory_stone_count > 0) {
                action_is_place = true;
                place_material_id = world::WorldServiceBasic::kMaterialStone;
                required_ticks = kPlaceRequiredTicks;
            } else if (
                state_.active_hotbar_row == 0 &&
                state_.selected_hotbar_slot == 4 &&
                target_material == world::WorldServiceBasic::kMaterialAir &&
                target_material != world::WorldServiceBasic::kMaterialWater &&
                state_.inventory_torch_count > 0) {
                action_is_place = true;
                place_material_id = world::WorldServiceBasic::kMaterialTorch;
                required_ticks = kPlaceRequiredTicks;
            } else if (
                state_.active_hotbar_row == 0 &&
                state_.selected_hotbar_slot == 5 &&
                target_material == world::WorldServiceBasic::kMaterialAir &&
                state_.inventory_workbench_count > 0) {
                action_is_place = true;
                place_material_id = world::WorldServiceBasic::kMaterialWorkbench;
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
                        std::uint16_t drop_material_id = 0;
                        if (TryResolveHarvestDrop(target_material, drop_material_id)) {
                            SpawnWorldDrop(target_tile_x, target_tile_y, drop_material_id, 1);
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
                        } else if (
                            place_material_id == world::WorldServiceBasic::kMaterialTorch &&
                            state_.inventory_torch_count > 0) {
                            --state_.inventory_torch_count;
                            submit_world_set_tile(target_tile_x, target_tile_y, place_material_id);
                        } else if (
                            place_material_id == world::WorldServiceBasic::kMaterialWorkbench &&
                            state_.inventory_workbench_count > 0) {
                            --state_.inventory_workbench_count;
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

    if (input_intent.interaction_primary_pressed) {
        if (state_.inventory_open) {
            auto is_workbench_reachable = [&world_service, this]() {
                constexpr int kWorkbenchReach = 4;
                for (int dy = -kWorkbenchReach; dy <= kWorkbenchReach; ++dy) {
                    for (int dx = -kWorkbenchReach; dx <= kWorkbenchReach; ++dx) {
                        const int tile_x = state_.tile_x + dx;
                        const int tile_y = state_.tile_y + dy;
                        std::uint16_t material_id = 0;
                        if (!world_service.TryReadTile(tile_x, tile_y, material_id) ||
                            material_id != world::WorldServiceBasic::kMaterialWorkbench) {
                            continue;
                        }

                        if (dx * dx + dy * dy <= kWorkbenchReach * kWorkbenchReach) {
                            return true;
                        }
                    }
                }

                return false;
            };

            if (state_.selected_recipe_index == 0 &&
                state_.inventory_wood_count >= kWorkbenchWoodCost) {
                state_.inventory_wood_count -= kWorkbenchWoodCost;
                ++state_.inventory_workbench_count;
                submit_gameplay_command(sim::command::kGameplayBuildWorkbench, "");
                state_.workbench_built = true;
            } else if (
                state_.selected_recipe_index == 1 &&
                state_.inventory_wood_count >= kWoodSwordWoodCost &&
                is_workbench_reachable()) {
                state_.inventory_wood_count -= kWoodSwordWoodCost;
                ++state_.inventory_wood_sword_count;
                submit_gameplay_command(sim::command::kGameplayCraftSword, "");
                state_.wood_sword_crafted = true;
            } else if (
                state_.selected_recipe_index == 2 &&
                state_.inventory_wood_count >= 1 &&
                state_.inventory_coal_count >= 1) {
                --state_.inventory_wood_count;
                --state_.inventory_coal_count;
                state_.inventory_torch_count += 4;
            }
        } else {
            const bool interaction_target_reachable = target_reachable;
            (void)interaction_target_reachable;
        }
    }

    constexpr std::uint16_t kPickupToastTicks = 90;
    for (std::size_t index = 0; index < world_drops_.size();) {
        const WorldDrop& drop = world_drops_[index];
        if (drop.tile_x != state_.tile_x || drop.tile_y != state_.tile_y) {
            ++index;
            continue;
        }

        if (drop.material_id == world::WorldServiceBasic::kMaterialDirt ||
            drop.material_id == world::WorldServiceBasic::kMaterialGrass) {
            state_.inventory_dirt_count += drop.amount;
        } else if (drop.material_id == world::WorldServiceBasic::kMaterialStone) {
            state_.inventory_stone_count += drop.amount;
            submit_collect_resource(sim::command::kResourceStone, drop.amount);
        } else if (drop.material_id == world::WorldServiceBasic::kMaterialCoalOre) {
            state_.inventory_coal_count += drop.amount;
        } else if (drop.material_id == world::WorldServiceBasic::kMaterialTorch) {
            state_.inventory_torch_count += drop.amount;
        } else if (drop.material_id == world::WorldServiceBasic::kMaterialWood) {
            state_.inventory_wood_count += drop.amount;
            submit_collect_resource(sim::command::kResourceWood, drop.amount);
        }

        state_.pickup_toast_material_id = drop.material_id;
        state_.pickup_toast_amount = drop.amount;
        state_.pickup_toast_ticks_remaining = kPickupToastTicks;

        world_drops_.erase(world_drops_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    (void)input_intent.interaction_primary_pressed;
    (void)submit_gameplay_command;
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
        material_id != world::WorldServiceBasic::kMaterialLeaves &&
        material_id != world::WorldServiceBasic::kMaterialTorch;
}

bool PlayerController::IsPickaxeHarvestMaterial(std::uint16_t material_id) {
    return material_id == world::WorldServiceBasic::kMaterialDirt ||
        material_id == world::WorldServiceBasic::kMaterialGrass ||
        material_id == world::WorldServiceBasic::kMaterialStone ||
        material_id == world::WorldServiceBasic::kMaterialCoalOre ||
        material_id == world::WorldServiceBasic::kMaterialTorch;
}

bool PlayerController::IsAxeHarvestMaterial(std::uint16_t material_id) {
    return material_id == world::WorldServiceBasic::kMaterialWood ||
        material_id == world::WorldServiceBasic::kMaterialLeaves;
}

int PlayerController::RequiredHarvestTicks(std::uint16_t material_id) {
    if (material_id == world::WorldServiceBasic::kMaterialStone) {
        return 18;
    }
    if (material_id == world::WorldServiceBasic::kMaterialCoalOre) {
        return 20;
    }
    if (material_id == world::WorldServiceBasic::kMaterialWood) {
        return 12;
    }
    if (material_id == world::WorldServiceBasic::kMaterialLeaves) {
        return 6;
    }
    if (material_id == world::WorldServiceBasic::kMaterialTorch) {
        return 4;
    }
    return 8;
}

bool PlayerController::TryResolveHarvestDrop(
    std::uint16_t material_id,
    std::uint16_t& out_drop_material_id) {
    if (material_id == world::WorldServiceBasic::kMaterialDirt ||
        material_id == world::WorldServiceBasic::kMaterialGrass) {
        out_drop_material_id = world::WorldServiceBasic::kMaterialDirt;
        return true;
    }
    if (material_id == world::WorldServiceBasic::kMaterialStone) {
        out_drop_material_id = world::WorldServiceBasic::kMaterialStone;
        return true;
    }
    if (material_id == world::WorldServiceBasic::kMaterialCoalOre) {
        out_drop_material_id = world::WorldServiceBasic::kMaterialCoalOre;
        return true;
    }
    if (material_id == world::WorldServiceBasic::kMaterialTorch) {
        out_drop_material_id = world::WorldServiceBasic::kMaterialTorch;
        return true;
    }
    if (material_id == world::WorldServiceBasic::kMaterialWood) {
        out_drop_material_id = world::WorldServiceBasic::kMaterialWood;
        return true;
    }

    return false;
}

void PlayerController::ResetPrimaryActionProgress() {
    primary_action_progress_ = {};
}

void PlayerController::SpawnWorldDrop(
    int tile_x,
    int tile_y,
    std::uint16_t material_id,
    std::uint32_t amount) {
    if (amount == 0) {
        return;
    }

    for (WorldDrop& drop : world_drops_) {
        if (drop.tile_x == tile_x &&
            drop.tile_y == tile_y &&
            drop.material_id == material_id) {
            drop.amount += amount;
            return;
        }
    }

    world_drops_.push_back(WorldDrop{
        .tile_x = tile_x,
        .tile_y = tile_y,
        .material_id = material_id,
        .amount = amount,
    });
}

}  // namespace novaria::app
