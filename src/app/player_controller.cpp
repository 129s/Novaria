#include "app/player_controller.h"

#include "app/player_controller_components.h"
#include "sim/command_schema.h"
#include "world/material_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace novaria::app {

void PlayerController::Reset() {
    state_ = {};
}

const LocalPlayerState& PlayerController::State() const {
    return state_;
}

void PlayerController::SyncFromSimulation(sim::SimulationKernel& simulation_kernel) {
    const sim::PlayerMotionSnapshot motion_snapshot =
        simulation_kernel.LocalPlayerMotion();
    state_.position_x = motion_snapshot.position_x;
    state_.position_y = motion_snapshot.position_y;
    state_.tile_x = static_cast<int>(std::floor(state_.position_x));
    state_.tile_y = static_cast<int>(std::floor(state_.position_y));

    const sim::PlayerInventorySnapshot inventory =
        simulation_kernel.InventorySnapshot(simulation_kernel.LocalPlayerId());
    state_.inventory_dirt_count = inventory.dirt_count;
    state_.inventory_stone_count = inventory.stone_count;
    state_.inventory_wood_count = inventory.wood_count;
    state_.inventory_coal_count = inventory.coal_count;
    state_.inventory_torch_count = inventory.torch_count;
    state_.inventory_workbench_count = inventory.workbench_count;
    state_.inventory_wood_sword_count = inventory.wood_sword_count;
    state_.has_pickaxe_tool = inventory.has_pickaxe_tool;
    state_.has_axe_tool = inventory.has_axe_tool;
}

void PlayerController::Update(
    const PlayerInputIntent& input_intent,
    world::IWorldService& world_service,
    sim::SimulationKernel& simulation_kernel,
    std::uint32_t local_player_id) {
    constexpr int kTilePixelSize = 32;
    constexpr int kReachDistanceTiles = 4;
    constexpr std::uint8_t kHotbarRows = 2;

    simulation_kernel.SetLocalPlayerId(local_player_id);

    if (state_.pickup_toast_ticks_remaining > 0) {
        --state_.pickup_toast_ticks_remaining;
        if (state_.pickup_toast_ticks_remaining == 0) {
            state_.pickup_toast_material_id = 0;
            state_.pickup_toast_amount = 0;
        }
    }
    if (state_.last_interaction_ticks_remaining > 0) {
        --state_.last_interaction_ticks_remaining;
        if (state_.last_interaction_ticks_remaining == 0) {
            state_.last_interaction_type = 0;
        }
    }

    const sim::GameplayProgressSnapshot gameplay_progress =
        simulation_kernel.GameplayProgress();
    state_.workbench_built = gameplay_progress.workbench_built;
    state_.wood_sword_crafted = gameplay_progress.sword_crafted;
    sim::command::PlayerMotionInputPayload motion_input_payload{};
    if (input_intent.move_left) {
        motion_input_payload.move_axis_milli -= 1000;
    }
    if (input_intent.move_right) {
        motion_input_payload.move_axis_milli += 1000;
    }
    if (input_intent.jump_pressed) {
        motion_input_payload.input_flags |= sim::command::kMotionInputFlagJumpPressed;
    }
    simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
        .player_id = local_player_id,
        .command_id = sim::command::kPlayerMotionInput,
        .payload = sim::command::EncodePlayerMotionInputPayload(motion_input_payload),
    });

    auto submit_world_load_chunk = [&simulation_kernel, local_player_id](int chunk_x, int chunk_y) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_id = sim::command::kWorldLoadChunk,
            .payload = sim::command::EncodeWorldChunkPayload(sim::command::WorldChunkPayload{
                .chunk_x = chunk_x,
                .chunk_y = chunk_y,
            }),
        });
    };

    auto submit_world_unload_chunk = [&simulation_kernel, local_player_id](int chunk_x, int chunk_y) {
        simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
            .player_id = local_player_id,
            .command_id = sim::command::kWorldUnloadChunk,
            .payload = sim::command::EncodeWorldChunkPayload(sim::command::WorldChunkPayload{
                .chunk_x = chunk_x,
                .chunk_y = chunk_y,
            }),
        });
    };

    auto submit_command =
        [&simulation_kernel, local_player_id](std::uint32_t command_id, wire::ByteBuffer payload) {
            simulation_kernel.SubmitLocalCommand(net::PlayerCommand{
                .player_id = local_player_id,
                .command_id = command_id,
                .payload = std::move(payload),
            });
        };

    auto submit_pickup_probe = [&submit_command](int tile_x, int tile_y) {
        submit_command(
            sim::command::kGameplayPickupProbe,
            sim::command::EncodePickupProbePayload(sim::command::PickupProbePayload{
                .tile_x = tile_x,
                .tile_y = tile_y,
                }));
    };

    constexpr int kChunkWindowRadius = 2;
    controller::UpdateChunkWindow(
        state_,
        kChunkWindowRadius,
        submit_world_load_chunk,
        submit_world_unload_chunk);


    auto apply_hotbar_slot = [this](std::uint8_t slot_index) {
        static constexpr std::array<std::uint16_t, 10> kSlotMaterialMapping = {
            world::material::kAir,
            world::material::kAir,
            world::material::kDirt,
            world::material::kStone,
            world::material::kTorch,
            world::material::kWorkbench,
            world::material::kAir,
            world::material::kAir,
            world::material::kAir,
            world::material::kAir,
        };

        state_.selected_hotbar_slot = slot_index;
        if (slot_index < kSlotMaterialMapping.size()) {
            const std::uint16_t mapped_material = kSlotMaterialMapping[slot_index];
            if (mapped_material != world::material::kAir) {
                state_.selected_place_material_id = mapped_material;
            }
        }
    };

    if (input_intent.ui_inventory_toggle_pressed) {
        state_.inventory_open = !state_.inventory_open;
    }

    controller::ApplyHotbarInput(
        state_,
        input_intent,
        kHotbarRows,
        apply_hotbar_slot);

    const controller::TargetResolution target_resolution = controller::ResolveTarget(
        state_,
        input_intent,
        kTilePixelSize,
        kReachDistanceTiles);
    const int target_tile_x = target_resolution.tile_x;
    const int target_tile_y = target_resolution.tile_y;

    if (target_tile_x < state_.tile_x) {
        state_.facing_x = -1;
    } else if (target_tile_x > state_.tile_x) {
        state_.facing_x = 1;
    }

    const bool target_reachable = target_resolution.reachable;
    state_.target_highlight_visible = input_intent.smart_context_held;
    state_.target_highlight_tile_x = target_tile_x;
    state_.target_highlight_tile_y = target_tile_y;

    if (input_intent.smart_mode_toggle_pressed) {
        state_.smart_mode_enabled = !state_.smart_mode_enabled;
    }
    if (state_.smart_mode_enabled && input_intent.smart_context_held) {
        state_.context_slot_visible = true;
        if (!state_.context_slot_override_active) {
            state_.context_slot_previous = state_.selected_hotbar_slot;
            state_.context_slot_override_active = true;
        }

        const std::uint8_t suggested_slot = controller::ResolveSmartContextSlot(
            state_,
            world_service,
            target_tile_x,
            target_tile_y);

        state_.context_slot_current = suggested_slot;
        apply_hotbar_slot(suggested_slot);
    } else {
        state_.context_slot_visible = false;
        if (state_.context_slot_override_active) {
            apply_hotbar_slot(state_.context_slot_previous);
            state_.context_slot_override_active = false;
        }
    state_.context_slot_current = state_.selected_hotbar_slot;
    }

    if (!state_.inventory_open && input_intent.action_primary_held && target_reachable) {
        submit_command(
            sim::command::kGameplayActionPrimary,
            sim::command::EncodeActionPrimaryPayload(sim::command::ActionPrimaryPayload{
                .target_tile_x = target_tile_x,
                .target_tile_y = target_tile_y,
                .hotbar_row = state_.active_hotbar_row,
                .hotbar_slot = state_.selected_hotbar_slot,
            }));
    }

    if (input_intent.interaction_primary_pressed) {
        std::uint16_t interaction_type = sim::command::kInteractionTypeNone;
        int interaction_tile_x = target_tile_x;
        int interaction_tile_y = target_tile_y;
        std::uint16_t interaction_target_material = 0;

        if (state_.inventory_open) {
            submit_command(
                sim::command::kGameplayCraftRecipe,
                sim::command::EncodeCraftRecipePayload(sim::command::CraftRecipePayload{
                    .recipe_index = state_.selected_recipe_index,
                }));

            interaction_type = sim::command::kInteractionTypeCraftRecipe;
            interaction_tile_x = state_.tile_x;
            interaction_tile_y = state_.tile_y;
            state_.last_interaction_type = 2;
            state_.last_interaction_ticks_remaining = 60;
        } else {
            interaction_type = sim::command::kInteractionTypeOpenCrafting;
            std::uint16_t interaction_result = sim::command::kInteractionResultRejected;
            if (target_reachable) {
                std::uint16_t target_material = 0;
                if (world_service.TryReadTile(target_tile_x, target_tile_y, target_material)) {
                    interaction_target_material = target_material;
                    if (target_material == world::material::kWorkbench) {
                        state_.inventory_open = true;
                        state_.last_interaction_type = 1;
                        state_.last_interaction_ticks_remaining = 60;
                        interaction_result = sim::command::kInteractionResultSuccess;
                    }
                }
            }

            submit_command(
                sim::command::kGameplayInteraction,
                sim::command::EncodeInteractionPayload(sim::command::InteractionPayload{
                    .interaction_type = interaction_type,
                    .target_tile_x = interaction_tile_x,
                    .target_tile_y = interaction_tile_y,
                    .target_material_id = interaction_target_material,
                    .result_code = interaction_result,
                }));
        }
    }

    submit_pickup_probe(state_.tile_x, state_.tile_y);

    constexpr std::uint16_t kPickupToastTicks = 90;
    for (const sim::GameplayPickupEvent& pickup_event :
         simulation_kernel.ConsumePickupEventsForPlayer(local_player_id)) {
        state_.pickup_toast_material_id = pickup_event.material_id;
        state_.pickup_toast_amount = pickup_event.amount;
        state_.pickup_toast_ticks_remaining = kPickupToastTicks;
        ++state_.pickup_event_counter;
    }
}

}  // namespace novaria::app
