#include "app/render_scene_builder.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace novaria::app {
namespace {

bool IsSunlightBlockingMaterial(std::uint16_t material_id) {
    return material_id != world::WorldServiceBasic::kMaterialAir &&
        material_id != world::WorldServiceBasic::kMaterialWater &&
        material_id != world::WorldServiceBasic::kMaterialLeaves &&
        material_id != world::WorldServiceBasic::kMaterialTorch;
}

}  // namespace

platform::RenderScene RenderSceneBuilder::Build(
    const LocalPlayerState& player_state,
    const core::GameConfig& config,
    const world::WorldServiceBasic& world_service,
    float daylight_factor) const {
    constexpr int kTilePixelSize = 32;
    platform::RenderScene scene{};
    scene.tile_pixel_size = kTilePixelSize;
    scene.camera_tile_x = player_state.tile_x;
    scene.camera_tile_y = player_state.tile_y;
    scene.view_tiles_x = std::max(1, config.window_width / kTilePixelSize);
    scene.view_tiles_y = std::max(1, config.window_height / kTilePixelSize);
    scene.player_tile_x = player_state.tile_x;
    scene.player_tile_y = player_state.tile_y;
    scene.hud = platform::RenderHudState{
        .dirt_count = player_state.inventory_dirt_count,
        .stone_count = player_state.inventory_stone_count,
        .wood_count = player_state.inventory_wood_count,
        .coal_count = player_state.inventory_coal_count,
        .torch_count = player_state.inventory_torch_count,
        .workbench_count = player_state.inventory_workbench_count,
        .wood_sword_count = player_state.inventory_wood_sword_count,
        .pickup_toast_material_id = player_state.pickup_toast_material_id,
        .pickup_toast_amount = player_state.pickup_toast_amount,
        .pickup_toast_ticks_remaining = player_state.pickup_toast_ticks_remaining,
        .inventory_open = player_state.inventory_open,
        .selected_recipe_index = player_state.selected_recipe_index,
        .selected_material_id = player_state.selected_place_material_id,
        .hotbar_row = player_state.active_hotbar_row,
        .hotbar_slot = player_state.selected_hotbar_slot,
        .workbench_built = player_state.workbench_built,
        .wood_sword_crafted = player_state.wood_sword_crafted,
    };
    scene.daylight_factor = std::clamp(daylight_factor, 0.0F, 1.0F);

    const int first_world_tile_x = scene.camera_tile_x - scene.view_tiles_x / 2;
    const int first_world_tile_y = scene.camera_tile_y - scene.view_tiles_y / 2;
    scene.tiles.reserve(static_cast<std::size_t>(scene.view_tiles_x * scene.view_tiles_y));
    std::vector<bool> column_blocked(static_cast<std::size_t>(scene.view_tiles_x), false);
    const float sky_light_base = 0.2F + scene.daylight_factor * 0.8F;
    std::vector<std::pair<int, int>> torch_tiles;
    std::vector<float> tile_light_factors;
    tile_light_factors.reserve(static_cast<std::size_t>(scene.view_tiles_x * scene.view_tiles_y));
    for (int local_y = 0; local_y < scene.view_tiles_y; ++local_y) {
        for (int local_x = 0; local_x < scene.view_tiles_x; ++local_x) {
            const int world_tile_x = first_world_tile_x + local_x;
            const int world_tile_y = first_world_tile_y + local_y;
            std::uint16_t material_id = 0;
            (void)world_service.TryReadTile(
                world_tile_x,
                world_tile_y,
                material_id);

            const bool is_blocked = column_blocked[static_cast<std::size_t>(local_x)];
            float light_factor = sky_light_base;
            if (is_blocked) {
                light_factor *= 0.36F;
            }
            if (!is_blocked && IsSunlightBlockingMaterial(material_id)) {
                column_blocked[static_cast<std::size_t>(local_x)] = true;
            }

            if (material_id == world::WorldServiceBasic::kMaterialTorch) {
                torch_tiles.push_back({local_x, local_y});
            }
            tile_light_factors.push_back(light_factor);
            scene.tiles.push_back(platform::RenderTile{
                .world_tile_x = world_tile_x,
                .world_tile_y = world_tile_y,
                .material_id = material_id,
                .light_level = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(light_factor * 255.0F), 0, 255)),
            });
        }
    }

    constexpr float kTorchRadius = 6.0F;
    constexpr float kTorchIntensity = 0.85F;
    for (std::size_t tile_index = 0; tile_index < scene.tiles.size(); ++tile_index) {
        const int local_x = static_cast<int>(tile_index % static_cast<std::size_t>(scene.view_tiles_x));
        const int local_y = static_cast<int>(tile_index / static_cast<std::size_t>(scene.view_tiles_x));
        float torch_light = 0.0F;
        for (const auto& torch_tile : torch_tiles) {
            const float delta_x = static_cast<float>(local_x - torch_tile.first);
            const float delta_y = static_cast<float>(local_y - torch_tile.second);
            const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
            if (distance > kTorchRadius) {
                continue;
            }

            const float falloff = 1.0F - (distance / kTorchRadius);
            torch_light = std::max(torch_light, falloff * kTorchIntensity);
        }

        const float combined_light = std::clamp(
            tile_light_factors[tile_index] + torch_light,
            0.0F,
            1.0F);
        scene.tiles[tile_index].light_level = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(combined_light * 255.0F), 0, 255));
    }

    return scene;
}

}  // namespace novaria::app
