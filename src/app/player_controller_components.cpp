#include "app/player_controller_components.h"

#include "world/material_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace novaria::app::controller {
namespace {

int FloorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

world::ChunkCoord TileToChunkCoord(int tile_x, int tile_y) {
    return world::ChunkCoord{
        .x = FloorDiv(tile_x, world::kChunkTileSize),
        .y = FloorDiv(tile_y, world::kChunkTileSize),
    };
}

}  // namespace

namespace {

enum class PrimaryActionKind : std::uint8_t {
    HarvestPickaxe,
    HarvestAxe,
    HarvestSword,
    PlaceDirt,
    PlaceStone,
    PlaceTorch,
    PlaceWorkbench,
};

struct HotbarActionSpec final {
    std::uint8_t row = 0;
    std::uint8_t slot = 0;
    PrimaryActionKind kind = PrimaryActionKind::HarvestPickaxe;
};

constexpr std::array<HotbarActionSpec, 7> kHotbarActions = {{
    {0, 0, PrimaryActionKind::HarvestPickaxe},
    {0, 1, PrimaryActionKind::HarvestAxe},
    {0, 6, PrimaryActionKind::HarvestSword},
    {0, 2, PrimaryActionKind::PlaceDirt},
    {0, 3, PrimaryActionKind::PlaceStone},
    {0, 4, PrimaryActionKind::PlaceTorch},
    {0, 5, PrimaryActionKind::PlaceWorkbench},
}};

}  // namespace

TargetResolution ResolveTarget(
    const LocalPlayerState& state,
    const PlayerInputIntent& input_intent,
    int tile_pixel_size,
    int reach_distance_tiles) {
    TargetResolution resolution{
        .tile_x = state.tile_x + state.facing_x,
        .tile_y = state.tile_y,
        .reachable = true,
    };

    if (input_intent.cursor_valid &&
        input_intent.viewport_width > 0 &&
        input_intent.viewport_height > 0) {
        const float half_view_px_x = static_cast<float>(input_intent.viewport_width) * 0.5F;
        const float half_view_px_y = static_cast<float>(input_intent.viewport_height) * 0.5F;
        const float world_x =
            state.position_x +
            (static_cast<float>(input_intent.cursor_screen_x) - half_view_px_x) /
                static_cast<float>(tile_pixel_size);
        const float world_y =
            state.position_y +
            (static_cast<float>(input_intent.cursor_screen_y) - half_view_px_y) /
                static_cast<float>(tile_pixel_size);
        resolution.tile_x = static_cast<int>(std::floor(world_x));
        resolution.tile_y = static_cast<int>(std::floor(world_y));
    }

    const int delta_x = resolution.tile_x - state.tile_x;
    const int delta_y = resolution.tile_y - state.tile_y;
    const int distance_squared = delta_x * delta_x + delta_y * delta_y;
    resolution.reachable = distance_squared <= (reach_distance_tiles * reach_distance_tiles);
    return resolution;
}

bool ResolvePrimaryActionPlan(
    const LocalPlayerState& state,
    std::uint16_t target_material_id,
    int place_required_ticks,
    PrimaryActionPlan& out_plan) {
    out_plan = {};
    for (const HotbarActionSpec& spec : kHotbarActions) {
        if (state.active_hotbar_row != spec.row ||
            state.selected_hotbar_slot != spec.slot) {
            continue;
        }

        switch (spec.kind) {
            case PrimaryActionKind::HarvestPickaxe:
                if (!state.has_pickaxe_tool ||
                    !world::material::IsHarvestableByPickaxe(target_material_id)) {
                    return false;
                }
                out_plan.is_harvest = true;
                out_plan.required_ticks = world::material::HarvestTicks(target_material_id);
                return true;
            case PrimaryActionKind::HarvestAxe:
                if (!state.has_axe_tool ||
                    !world::material::IsHarvestableByAxe(target_material_id)) {
                    return false;
                }
                out_plan.is_harvest = true;
                out_plan.required_ticks = world::material::HarvestTicks(target_material_id);
                return true;
            case PrimaryActionKind::HarvestSword:
                if (state.inventory_wood_sword_count == 0 ||
                    !world::material::IsHarvestableBySword(target_material_id)) {
                    return false;
                }
                out_plan.is_harvest = true;
                out_plan.required_ticks = world::material::HarvestTicks(target_material_id) + 10;
                return true;
            case PrimaryActionKind::PlaceDirt:
                if (target_material_id != world::material::kAir ||
                    state.inventory_dirt_count == 0) {
                    return false;
                }
                out_plan.is_place = true;
                out_plan.place_material_id = world::material::kDirt;
                out_plan.required_ticks = place_required_ticks;
                return true;
            case PrimaryActionKind::PlaceStone:
                if (target_material_id != world::material::kAir ||
                    state.inventory_stone_count == 0) {
                    return false;
                }
                out_plan.is_place = true;
                out_plan.place_material_id = world::material::kStone;
                out_plan.required_ticks = place_required_ticks;
                return true;
            case PrimaryActionKind::PlaceTorch:
                if (target_material_id != world::material::kAir ||
                    state.inventory_torch_count == 0) {
                    return false;
                }
                out_plan.is_place = true;
                out_plan.place_material_id = world::material::kTorch;
                out_plan.required_ticks = place_required_ticks;
                return true;
            case PrimaryActionKind::PlaceWorkbench:
                if (target_material_id != world::material::kAir ||
                    state.inventory_workbench_count == 0) {
                    return false;
                }
                out_plan.is_place = true;
                out_plan.place_material_id = world::material::kWorkbench;
                out_plan.required_ticks = place_required_ticks;
                return true;
        }
    }

    return false;
}

void UpdateChunkWindow(
    LocalPlayerState& state,
    int chunk_window_radius,
    const std::function<void(int, int)>& submit_world_load_chunk,
    const std::function<void(int, int)>& submit_world_unload_chunk) {
    const world::ChunkCoord player_chunk = TileToChunkCoord(state.tile_x, state.tile_y);
    if (state.loaded_chunk_window_ready &&
        player_chunk.x == state.loaded_chunk_window_center_x &&
        player_chunk.y == state.loaded_chunk_window_center_y) {
        return;
    }

    auto is_chunk_in_window = [chunk_window_radius](int chunk_x, int chunk_y, int center_x, int center_y) {
        return chunk_x >= center_x - chunk_window_radius &&
            chunk_x <= center_x + chunk_window_radius &&
            chunk_y >= center_y - chunk_window_radius &&
            chunk_y <= center_y + chunk_window_radius;
    };

    if (state.loaded_chunk_window_ready) {
        const int previous_center_x = state.loaded_chunk_window_center_x;
        const int previous_center_y = state.loaded_chunk_window_center_y;
        for (int chunk_y = previous_center_y - chunk_window_radius;
             chunk_y <= previous_center_y + chunk_window_radius;
             ++chunk_y) {
            for (int chunk_x = previous_center_x - chunk_window_radius;
                 chunk_x <= previous_center_x + chunk_window_radius;
                 ++chunk_x) {
                if (is_chunk_in_window(chunk_x, chunk_y, player_chunk.x, player_chunk.y)) {
                    continue;
                }

                submit_world_unload_chunk(chunk_x, chunk_y);
            }
        }
    }

    for (int chunk_y = player_chunk.y - chunk_window_radius;
         chunk_y <= player_chunk.y + chunk_window_radius;
         ++chunk_y) {
        for (int chunk_x = player_chunk.x - chunk_window_radius;
             chunk_x <= player_chunk.x + chunk_window_radius;
             ++chunk_x) {
            if (state.loaded_chunk_window_ready &&
                is_chunk_in_window(
                    chunk_x,
                    chunk_y,
                    state.loaded_chunk_window_center_x,
                    state.loaded_chunk_window_center_y)) {
                continue;
            }

            submit_world_load_chunk(chunk_x, chunk_y);
        }
    }

    state.loaded_chunk_window_ready = true;
    state.loaded_chunk_window_center_x = player_chunk.x;
    state.loaded_chunk_window_center_y = player_chunk.y;
}

void ApplyHotbarInput(
    LocalPlayerState& state,
    const PlayerInputIntent& input_intent,
    std::uint8_t hotbar_rows,
    const std::function<void(std::uint8_t)>& apply_hotbar_slot) {
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
        apply_hotbar_slot(static_cast<std::uint8_t>((state.selected_hotbar_slot + 9) % 10));
    } else if (input_intent.hotbar_cycle_next) {
        apply_hotbar_slot(static_cast<std::uint8_t>((state.selected_hotbar_slot + 1) % 10));
    }
    if (input_intent.hotbar_select_next_row && hotbar_rows > 0) {
        state.active_hotbar_row =
            static_cast<std::uint8_t>((state.active_hotbar_row + 1) % hotbar_rows);
    }
}

void ApplyInventoryUiInput(
    LocalPlayerState& state,
    const PlayerInputIntent& input_intent,
    int recipe_count) {
    if (!state.inventory_open || recipe_count <= 0) {
        return;
    }

    int selected = static_cast<int>(state.selected_recipe_index);
    selected = std::clamp(selected, 0, recipe_count - 1);

    auto apply_recipe_index = [&](int index) {
        selected = std::clamp(index, 0, recipe_count - 1);
    };

    if (input_intent.hotbar_select_slot_1) {
        apply_recipe_index(0);
    } else if (input_intent.hotbar_select_slot_2) {
        apply_recipe_index(1);
    } else if (input_intent.hotbar_select_slot_3) {
        apply_recipe_index(2);
    } else if (input_intent.hotbar_select_slot_4) {
        apply_recipe_index(3);
    } else if (input_intent.hotbar_select_slot_5) {
        apply_recipe_index(4);
    } else if (input_intent.hotbar_select_slot_6) {
        apply_recipe_index(5);
    } else if (input_intent.hotbar_select_slot_7) {
        apply_recipe_index(6);
    } else if (input_intent.hotbar_select_slot_8) {
        apply_recipe_index(7);
    } else if (input_intent.hotbar_select_slot_9) {
        apply_recipe_index(8);
    } else if (input_intent.hotbar_select_slot_10) {
        apply_recipe_index(9);
    } else if (input_intent.ui_nav_up_pressed) {
        selected = (selected - 1 + recipe_count) % recipe_count;
    } else if (input_intent.ui_nav_down_pressed) {
        selected = (selected + 1) % recipe_count;
    }

    state.selected_recipe_index = static_cast<std::uint8_t>(selected);
}

bool IsWorkbenchInRange(
    const LocalPlayerState& state,
    const world::IWorldService& world_service,
    int reach_distance_tiles) {
    if (reach_distance_tiles <= 0) {
        return false;
    }

    const int radius = reach_distance_tiles;
    const float reach = static_cast<float>(reach_distance_tiles);
    const float reach_sq = reach * reach;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int tile_x = state.tile_x + dx;
            const int tile_y = state.tile_y + dy;
            std::uint16_t material_id = 0;
            if (!world_service.TryReadTile(tile_x, tile_y, material_id)) {
                continue;
            }
            if (material_id != world::material::kWorkbench) {
                continue;
            }

            const float tile_center_x = static_cast<float>(tile_x) + 0.5F;
            const float tile_center_y = static_cast<float>(tile_y) + 0.5F;
            const float delta_x = state.position_x - tile_center_x;
            const float delta_y = state.position_y - tile_center_y;
            const float dist_sq = delta_x * delta_x + delta_y * delta_y;
            if (dist_sq <= reach_sq) {
                return true;
            }
        }
    }

    return false;
}

std::uint8_t ResolveSmartContextSlot(
    const LocalPlayerState& state,
    const world::IWorldService& world_service,
    int target_tile_x,
    int target_tile_y) {
    std::uint8_t suggested_slot = state.selected_hotbar_slot;
    std::uint16_t target_material_id = 0;
    if (!world_service.TryReadTile(target_tile_x, target_tile_y, target_material_id)) {
        return suggested_slot;
    }

    if (world::material::IsHarvestableByPickaxe(target_material_id)) {
        return 0;
    }
    if (world::material::IsHarvestableByAxe(target_material_id)) {
        return 1;
    }
    if (target_material_id == world::material::kAir && state.inventory_torch_count > 0) {
        return 4;
    }

    return suggested_slot;
}

}  // namespace novaria::app::controller
