#pragma once

#include "app/input_command_mapper.h"
#include "app/player_controller.h"
#include "world/world_service.h"

#include <cstdint>
#include <functional>

namespace novaria::app::controller {

struct TargetResolution final {
    int tile_x = 0;
    int tile_y = 0;
    bool reachable = false;
};

struct PrimaryActionPlan final {
    bool is_harvest = false;
    bool is_place = false;
    int required_ticks = 0;
    std::uint16_t place_material_id = 0;
};

TargetResolution ResolveTarget(
    const LocalPlayerState& state,
    const PlayerInputIntent& input_intent,
    int tile_pixel_size,
    int reach_distance_tiles);

bool ResolvePrimaryActionPlan(
    const LocalPlayerState& state,
    std::uint16_t target_material_id,
    int place_required_ticks,
    PrimaryActionPlan& out_plan);

void UpdateChunkWindow(
    LocalPlayerState& state,
    int chunk_window_radius,
    const std::function<void(int, int)>& submit_world_load_chunk,
    const std::function<void(int, int)>& submit_world_unload_chunk);

void ApplyHotbarInput(
    LocalPlayerState& state,
    const PlayerInputIntent& input_intent,
    std::uint8_t hotbar_rows,
    const std::function<void(std::uint8_t)>& apply_hotbar_slot);

std::uint8_t ResolveSmartContextSlot(
    const LocalPlayerState& state,
    const world::IWorldService& world_service,
    int target_tile_x,
    int target_tile_y);

}  // namespace novaria::app::controller
