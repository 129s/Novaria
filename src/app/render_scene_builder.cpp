#include "app/render_scene_builder.h"
#include "sim/player_motion.h"
#include "ui/gameplay_ui.h"
#include "world/material_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace novaria::app {
namespace {

struct RenderPalette final {
    platform::RgbaColor dirt_color{};
    platform::RgbaColor stone_color{};
    platform::RgbaColor wood_color{};
    platform::RgbaColor coal_color{};
    platform::RgbaColor torch_color{};
    platform::RgbaColor workbench_color{};
};

platform::RgbaColor ToPlatformColor(world::material::RgbaColor color) {
    return platform::RgbaColor{
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
}

std::uint8_t ClampToByte(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

platform::RgbaColor ApplyTileVariation(
    platform::RgbaColor base_color,
    int world_tile_x,
    int world_tile_y) {
    const int shade_delta = ((world_tile_x + world_tile_y) & 1) == 0 ? 8 : -8;
    base_color.r = ClampToByte(static_cast<int>(base_color.r) + shade_delta);
    base_color.g = ClampToByte(static_cast<int>(base_color.g) + shade_delta);
    base_color.b = ClampToByte(static_cast<int>(base_color.b) + shade_delta);
    return base_color;
}

platform::RgbaColor ApplyLight(platform::RgbaColor base_color, std::uint8_t light_level) {
    base_color.r = static_cast<std::uint8_t>(
        (static_cast<unsigned int>(base_color.r) * light_level) / 255U);
    base_color.g = static_cast<std::uint8_t>(
        (static_cast<unsigned int>(base_color.g) * light_level) / 255U);
    base_color.b = static_cast<std::uint8_t>(
        (static_cast<unsigned int>(base_color.b) * light_level) / 255U);
    return base_color;
}

bool IsSunlightBlockingMaterial(std::uint16_t material_id) {
    return world::material::BlocksSunlight(material_id);
}

}  // namespace

platform::RenderScene RenderSceneBuilder::Build(
    const LocalPlayerState& player_state,
    int viewport_width,
    int viewport_height,
    const world::IWorldService& world_service,
    float daylight_factor) const {
    constexpr int kTilePixelSize = 32;
    platform::RenderScene scene{};
    scene.tile_pixel_size = kTilePixelSize;
    scene.camera_tile_x = player_state.position_x;
    scene.camera_tile_y = player_state.position_y;
    const int base_view_tiles_x =
        std::max(1, (viewport_width + kTilePixelSize - 1) / kTilePixelSize);
    const int base_view_tiles_y =
        std::max(1, (viewport_height + kTilePixelSize - 1) / kTilePixelSize);
    scene.view_tiles_x = base_view_tiles_x + 2;
    scene.view_tiles_y = base_view_tiles_y + 2;

    const RenderPalette palette{
        .dirt_color = ToPlatformColor(world::material::BaseColor(world::material::kDirt)),
        .stone_color = ToPlatformColor(world::material::BaseColor(world::material::kStone)),
        .wood_color = ToPlatformColor(world::material::BaseColor(world::material::kWood)),
        .coal_color = ToPlatformColor(world::material::BaseColor(world::material::kCoalOre)),
        .torch_color = ToPlatformColor(world::material::BaseColor(world::material::kTorch)),
        .workbench_color = ToPlatformColor(world::material::BaseColor(world::material::kWorkbench)),
    };
    scene.daylight_factor = std::clamp(daylight_factor, 0.0F, 1.0F);

    const int camera_tile_floor_x = static_cast<int>(std::floor(scene.camera_tile_x));
    const int camera_tile_floor_y = static_cast<int>(std::floor(scene.camera_tile_y));
    const int first_world_tile_x = camera_tile_floor_x - scene.view_tiles_x / 2;
    const int first_world_tile_y = camera_tile_floor_y - scene.view_tiles_y / 2;
    scene.tiles.reserve(static_cast<std::size_t>(scene.view_tiles_x * scene.view_tiles_y));
    std::vector<bool> column_blocked(static_cast<std::size_t>(scene.view_tiles_x), false);
    const float sky_light_base = 0.2F + scene.daylight_factor * 0.8F;
    std::vector<std::pair<int, int>> torch_tiles;
    std::vector<float> tile_light_factors;
    std::vector<platform::RgbaColor> tile_base_colors;
    tile_light_factors.reserve(static_cast<std::size_t>(scene.view_tiles_x * scene.view_tiles_y));
    tile_base_colors.reserve(static_cast<std::size_t>(scene.view_tiles_x * scene.view_tiles_y));
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

            if (material_id == world::material::kTorch) {
                torch_tiles.push_back({local_x, local_y});
            }
            tile_light_factors.push_back(light_factor);
            const std::uint8_t light_level = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(light_factor * 255.0F), 0, 255));
            const platform::RgbaColor material_color =
                ToPlatformColor(world::material::BaseColor(material_id));
            const platform::RgbaColor varied_color =
                ApplyTileVariation(material_color, world_tile_x, world_tile_y);
            tile_base_colors.push_back(varied_color);
            scene.tiles.push_back(platform::RenderTile{
                .world_tile_x = world_tile_x,
                .world_tile_y = world_tile_y,
                .light_level = light_level,
                .color = ApplyLight(varied_color, light_level),
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
        const std::uint8_t light_level = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(combined_light * 255.0F), 0, 255));
        scene.tiles[tile_index].light_level = light_level;
        scene.tiles[tile_index].color = ApplyLight(tile_base_colors[tile_index], light_level);
    }

    const float tile_pixel_size_f = static_cast<float>(scene.tile_pixel_size);
    const float world_origin_x =
        static_cast<float>(viewport_width) * 0.5F -
        scene.camera_tile_x * tile_pixel_size_f;
    const float world_origin_y =
        static_cast<float>(viewport_height) * 0.5F -
        scene.camera_tile_y * tile_pixel_size_f;

    scene.overlay_commands.clear();
    scene.overlay_commands.reserve(256);

    const ui::GameplayUiPalette ui_palette{
        .text = platform::RgbaColor{.r = 232, .g = 232, .b = 236, .a = 240},
        .text_muted = platform::RgbaColor{.r = 184, .g = 184, .b = 194, .a = 230},
        .panel = platform::RgbaColor{.r = 18, .g = 18, .b = 22, .a = 220},
        .panel_2 = platform::RgbaColor{.r = 24, .g = 24, .b = 30, .a = 220},
        .border = platform::RgbaColor{.r = 68, .g = 68, .b = 78, .a = 220},
        .accent = platform::RgbaColor{.r = 88, .g = 184, .b = 226, .a = 255},
        .danger = platform::RgbaColor{.r = 210, .g = 66, .b = 78, .a = 255},
        .success = platform::RgbaColor{.r = 104, .g = 188, .b = 98, .a = 255},
        .dirt = palette.dirt_color,
        .stone = palette.stone_color,
        .wood = palette.wood_color,
        .coal = palette.coal_color,
        .torch = palette.torch_color,
        .workbench = palette.workbench_color,
    };

    const ui::GameplayUiFrameContext ui_frame{
        .viewport_width = viewport_width,
        .viewport_height = viewport_height,
        .world_origin_x = world_origin_x,
        .world_origin_y = world_origin_y,
        .tile_pixel_size = scene.tile_pixel_size,
    };

    std::string pickup_label;
    if (player_state.pickup_toast_material_id != 0) {
        pickup_label = world::material::Traits(player_state.pickup_toast_material_id).debug_name;
    }

    const ui::GameplayUiModel ui_model{
        .hp_current = player_state.hp_current,
        .hp_max = player_state.hp_max,
        .dirt_count = player_state.inventory_dirt_count,
        .stone_count = player_state.inventory_stone_count,
        .wood_count = player_state.inventory_wood_count,
        .coal_count = player_state.inventory_coal_count,
        .torch_count = player_state.inventory_torch_count,
        .workbench_count = player_state.inventory_workbench_count,
        .wood_sword_count = player_state.inventory_wood_sword_count,
        .workbench_built = player_state.workbench_built,
        .wood_sword_crafted = player_state.wood_sword_crafted,
        .hotbar_rows = 2,
        .active_hotbar_row = player_state.active_hotbar_row,
        .selected_hotbar_slot = player_state.selected_hotbar_slot,
        .smart_mode_enabled = player_state.smart_mode_enabled,
        .context_slot_visible = player_state.context_slot_visible,
        .context_slot_current = player_state.context_slot_current,
        .target_highlight_visible = player_state.target_highlight_visible,
        .target_tile_x = player_state.target_highlight_tile_x,
        .target_tile_y = player_state.target_highlight_tile_y,
        .target_reachable = player_state.target_reachable,
        .target_material_id = player_state.target_material_id,
        .selected_place_material_id = player_state.selected_place_material_id,
        .inventory_open = player_state.inventory_open,
        .selected_recipe_index = player_state.selected_recipe_index,
        .workbench_in_range = player_state.workbench_in_range,
        .pickup_toast_material_id = player_state.pickup_toast_material_id,
        .pickup_toast_amount = player_state.pickup_toast_amount,
        .pickup_toast_ticks_remaining = player_state.pickup_toast_ticks_remaining,
        .pickup_toast_label = std::move(pickup_label),
        .last_interaction_type = player_state.last_interaction_type,
        .last_interaction_ticks_remaining = player_state.last_interaction_ticks_remaining,
        .action_primary_progress = player_state.action_primary_progress,
    };

    ui::AppendGameplayUi(scene, ui_frame, ui_palette, ui_model);

    return scene;
}

}  // namespace novaria::app
