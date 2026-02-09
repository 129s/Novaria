#pragma once

#include "app/input_command_mapper.h"
#include "sim/simulation_kernel.h"
#include "world/world_service.h"

#include <cstdint>

namespace novaria::app {

struct LocalPlayerState final {
    float position_x = 0.0F;
    float position_y = -2.0F;
    int tile_x = 0;
    int tile_y = -2;
    int facing_x = 1;
    std::uint16_t hp_current = 100;
    std::uint16_t hp_max = 100;
    std::uint32_t inventory_dirt_count = 0;
    std::uint32_t inventory_stone_count = 0;
    std::uint32_t inventory_wood_count = 0;
    std::uint32_t inventory_coal_count = 0;
    std::uint32_t inventory_torch_count = 0;
    std::uint32_t inventory_workbench_count = 0;
    std::uint32_t inventory_wood_sword_count = 0;
    bool has_pickaxe_tool = true;
    bool has_axe_tool = true;
    std::uint32_t pickup_event_counter = 0;
    std::uint16_t pickup_toast_material_id = 0;
    std::uint32_t pickup_toast_amount = 0;
    std::uint16_t pickup_toast_ticks_remaining = 0;
    bool inventory_open = false;
    std::uint8_t selected_recipe_index = 0;
    bool smart_mode_enabled = false;
    bool context_slot_visible = false;
    bool context_slot_override_active = false;
    std::uint8_t context_slot_previous = 0;
    std::uint8_t context_slot_current = 0;
    bool target_highlight_visible = false;
    int target_highlight_tile_x = 0;
    int target_highlight_tile_y = 0;
    std::uint8_t last_interaction_type = 0;
    std::uint16_t last_interaction_ticks_remaining = 0;
    std::uint8_t active_hotbar_row = 0;
    std::uint8_t selected_hotbar_slot = 0;
    std::uint16_t selected_place_material_id = 1;
    bool workbench_built = false;
    bool wood_sword_crafted = false;
    bool loaded_chunk_window_ready = false;
    int loaded_chunk_window_center_x = 0;
    int loaded_chunk_window_center_y = 0;
};

class PlayerController final {
public:
    void Reset();
    const LocalPlayerState& State() const;
    void SyncFromSimulation(sim::SimulationKernel& simulation_kernel);
    void Update(
        const PlayerInputIntent& input_intent,
        world::IWorldService& world_service,
        sim::SimulationKernel& simulation_kernel,
        std::uint32_t local_player_id);

private:
    LocalPlayerState state_{};
};

}  // namespace novaria::app
