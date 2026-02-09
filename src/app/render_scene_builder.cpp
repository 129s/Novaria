#include "app/render_scene_builder.h"
#include "sim/player_motion.h"
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

platform::RgbaColor WithAlpha(platform::RgbaColor color, std::uint8_t alpha) {
    color.a = alpha;
    return color;
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

platform::RgbaColor ToPickupToastColor(const RenderPalette& palette, std::uint16_t material_id) {
    platform::RgbaColor color{.r = 196, .g = 196, .b = 196, .a = 240};
    if (material_id == world::material::kDirt ||
        material_id == world::material::kGrass) {
        color = WithAlpha(palette.dirt_color, 240);
    } else if (material_id == world::material::kStone) {
        color = WithAlpha(palette.stone_color, 240);
    } else if (material_id == world::material::kCoalOre) {
        color = WithAlpha(palette.coal_color, 240);
    } else if (material_id == world::material::kTorch) {
        color = WithAlpha(palette.torch_color, 240);
    } else if (material_id == world::material::kWood) {
        color = WithAlpha(palette.wood_color, 240);
    }

    return color;
}

void PushFilledRect(
    platform::RenderScene& scene,
    platform::RenderLayer layer,
    int z,
    float x,
    float y,
    float width,
    float height,
    platform::RgbaColor color) {
    scene.overlay_commands.push_back(
        platform::RenderCommand::FilledRect(layer, z, x, y, width, height, color));
}

void PushText(
    platform::RenderScene& scene,
    platform::RenderLayer layer,
    int z,
    float x,
    float y,
    float scale,
    std::string text,
    platform::RgbaColor color) {
    scene.overlay_commands.push_back(
        platform::RenderCommand::Text(layer, z, x, y, scale, std::move(text), color));
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
    const sim::PlayerMotionSettings& motion_settings = sim::DefaultPlayerMotionSettings();

    const RenderPalette palette{
        .dirt_color = ToPlatformColor(world::material::BaseColor(world::material::kDirt)),
        .stone_color = ToPlatformColor(world::material::BaseColor(world::material::kStone)),
        .wood_color = ToPlatformColor(world::material::BaseColor(world::material::kWood)),
        .coal_color = ToPlatformColor(world::material::BaseColor(world::material::kCoalOre)),
        .torch_color = ToPlatformColor(world::material::BaseColor(world::material::kTorch)),
        .workbench_color = ToPlatformColor(world::material::BaseColor(world::material::kWorkbench)),
    };
    const platform::RgbaColor pickup_toast_color =
        ToPickupToastColor(palette, player_state.pickup_toast_material_id);
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
    scene.overlay_commands.reserve(72);

    if (player_state.target_highlight_visible) {
        PushFilledRect(
            scene,
            platform::RenderLayer::WorldOverlay,
            0,
            world_origin_x + static_cast<float>(player_state.target_highlight_tile_x) * tile_pixel_size_f,
            world_origin_y + static_cast<float>(player_state.target_highlight_tile_y) * tile_pixel_size_f,
            tile_pixel_size_f,
            tile_pixel_size_f,
            platform::RgbaColor{.r = 240, .g = 214, .b = 108, .a = 72});
    }

    {
        const float player_collision_left =
            world_origin_x +
            (player_state.position_x - motion_settings.half_width) *
                tile_pixel_size_f;
        const float player_collision_top =
            world_origin_y +
            (player_state.position_y - motion_settings.height) *
                tile_pixel_size_f;
        const float player_collision_width =
            motion_settings.half_width * 2.0F * tile_pixel_size_f;
        const float player_collision_height_px =
            motion_settings.height * tile_pixel_size_f;
        PushFilledRect(
            scene,
            platform::RenderLayer::Debug,
            0,
            player_collision_left,
            player_collision_top,
            player_collision_width,
            player_collision_height_px,
            platform::RgbaColor{.r = 72, .g = 196, .b = 248, .a = 140});
    }

    const int hud_x = 12;
    const int hud_y = 12;
    const int hud_width = 280;
    const int hud_height = 180;
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        0,
        static_cast<float>(hud_x),
        static_cast<float>(hud_y),
        static_cast<float>(hud_width),
        static_cast<float>(hud_height),
        platform::RgbaColor{.r = 18, .g = 18, .b = 22, .a = 214});

    {
        const platform::RgbaColor hud_text{.r = 222, .g = 222, .b = 226, .a = 240};
        PushText(
            scene,
            platform::RenderLayer::UI,
            10,
            static_cast<float>(hud_x + 12),
            static_cast<float>(hud_y + 8),
            2.0F,
            "HP " + std::to_string(player_state.hp_current) + "/" + std::to_string(player_state.hp_max),
            hud_text);
        PushText(
            scene,
            platform::RenderLayer::UI,
            10,
            static_cast<float>(hud_x + 12),
            static_cast<float>(hud_y + 28),
            2.0F,
            "DIRT " + std::to_string(player_state.inventory_dirt_count),
            hud_text);
        PushText(
            scene,
            platform::RenderLayer::UI,
            10,
            static_cast<float>(hud_x + 12),
            static_cast<float>(hud_y + 44),
            2.0F,
            "STONE " + std::to_string(player_state.inventory_stone_count),
            hud_text);
        PushText(
            scene,
            platform::RenderLayer::UI,
            10,
            static_cast<float>(hud_x + 12),
            static_cast<float>(hud_y + 60),
            2.0F,
            "WOOD " + std::to_string(player_state.inventory_wood_count),
            hud_text);
        PushText(
            scene,
            platform::RenderLayer::UI,
            10,
            static_cast<float>(hud_x + 12),
            static_cast<float>(hud_y + 76),
            2.0F,
            "COAL " + std::to_string(player_state.inventory_coal_count),
            hud_text);
        PushText(
            scene,
            platform::RenderLayer::UI,
            10,
            static_cast<float>(hud_x + 12),
            static_cast<float>(hud_y + 92),
            2.0F,
            "TORCH " + std::to_string(player_state.inventory_torch_count),
            hud_text);
    }

    const int bar_max_width = 136;
    const int hp_bar_width = std::min<int>(
        bar_max_width,
        player_state.hp_max == 0
            ? 0
            : static_cast<int>((static_cast<unsigned long long>(player_state.hp_current) * bar_max_width) /
                               player_state.hp_max));
    const int dirt_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(player_state.inventory_dirt_count) * 4);
    const int stone_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(player_state.inventory_stone_count) * 4);
    const int wood_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(player_state.inventory_wood_count) * 4);
    const int coal_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(player_state.inventory_coal_count) * 4);
    const int torch_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(player_state.inventory_torch_count) * 4);
    const int workbench_item_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(player_state.inventory_workbench_count) * 32);
    const int wood_sword_item_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(player_state.inventory_wood_sword_count) * 32);

    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y - 6),
        static_cast<float>(hp_bar_width),
        10.0F,
        platform::RgbaColor{.r = 204, .g = 62, .b = 74, .a = 255});
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y + 14),
        static_cast<float>(dirt_bar_width),
        12.0F,
        palette.dirt_color);
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y + 34),
        static_cast<float>(stone_bar_width),
        12.0F,
        palette.stone_color);
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y + 54),
        static_cast<float>(wood_bar_width),
        12.0F,
        palette.wood_color);
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y + 74),
        static_cast<float>(coal_bar_width),
        12.0F,
        palette.coal_color);
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y + 94),
        static_cast<float>(torch_bar_width),
        12.0F,
        palette.torch_color);
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y + 114),
        static_cast<float>(workbench_item_bar_width),
        12.0F,
        palette.workbench_color);
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(hud_x + 96),
        static_cast<float>(hud_y + 134),
        static_cast<float>(wood_sword_item_bar_width),
        12.0F,
        platform::RgbaColor{.r = 186, .g = 210, .b = 228, .a = 255});

    const int selector_x = hud_x + 18;
    const int selector_y = hud_y + 16;
    const bool dirt_selected = player_state.selected_place_material_id == world::material::kDirt;
    const bool stone_selected = player_state.selected_place_material_id == world::material::kStone;
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(selector_x - 2),
        static_cast<float>(selector_y - 2),
        24.0F,
        24.0F,
        dirt_selected
            ? platform::RgbaColor{.r = 240, .g = 214, .b = 108, .a = 255}
            : platform::RgbaColor{.r = 36, .g = 36, .b = 38, .a = 255});
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(selector_x + 26),
        static_cast<float>(selector_y - 2),
        24.0F,
        24.0F,
        stone_selected
            ? platform::RgbaColor{.r = 240, .g = 214, .b = 108, .a = 255}
            : platform::RgbaColor{.r = 36, .g = 36, .b = 38, .a = 255});
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        2,
        static_cast<float>(selector_x),
        static_cast<float>(selector_y),
        20.0F,
        20.0F,
        palette.dirt_color);
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        2,
        static_cast<float>(selector_x + 28),
        static_cast<float>(selector_y),
        20.0F,
        20.0F,
        palette.stone_color);

    for (int slot = 0; slot < 10; ++slot) {
        const int slot_x = hud_x + 8 + slot * 14;
        const int slot_y = hud_y + 128 + static_cast<int>(player_state.active_hotbar_row) * 10;
        PushFilledRect(
            scene,
            platform::RenderLayer::UI,
            1,
            static_cast<float>(slot_x),
            static_cast<float>(slot_y),
            10.0F,
            6.0F,
            slot == static_cast<int>(player_state.selected_hotbar_slot)
                ? platform::RgbaColor{.r = 230, .g = 208, .b = 114, .a = 255}
                : platform::RgbaColor{.r = 78, .g = 78, .b = 84, .a = 255});
    }

    const int status_x = hud_x + 16;
    const int status_y = hud_y + 154;
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(status_x),
        static_cast<float>(status_y),
        116.0F,
        12.0F,
        player_state.workbench_built
            ? platform::RgbaColor{.r = 104, .g = 188, .b = 98, .a = 255}
            : platform::RgbaColor{.r = 70, .g = 42, .b = 28, .a = 255});
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(status_x + 128),
        static_cast<float>(status_y),
        116.0F,
        12.0F,
        player_state.wood_sword_crafted
            ? platform::RgbaColor{.r = 186, .g = 210, .b = 228, .a = 255}
            : platform::RgbaColor{.r = 54, .g = 58, .b = 64, .a = 255});
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(status_x),
        static_cast<float>(status_y + 18),
        116.0F,
        10.0F,
        player_state.smart_mode_enabled
            ? platform::RgbaColor{.r = 88, .g = 184, .b = 226, .a = 255}
            : platform::RgbaColor{.r = 62, .g = 66, .b = 72, .a = 255});
    PushFilledRect(
        scene,
        platform::RenderLayer::UI,
        1,
        static_cast<float>(status_x + 128),
        static_cast<float>(status_y + 18),
        116.0F,
        10.0F,
        player_state.context_slot_visible
            ? platform::RgbaColor{.r = 236, .g = 196, .b = 94, .a = 255}
            : platform::RgbaColor{.r = 62, .g = 66, .b = 72, .a = 255});

    if (player_state.pickup_toast_ticks_remaining > 0) {
        const int pickup_bar_width = std::min<int>(
            220,
            18 + static_cast<int>(player_state.pickup_toast_amount) * 10);
        PushFilledRect(
            scene,
            platform::RenderLayer::UI,
            3,
            static_cast<float>(hud_x + 16),
            static_cast<float>(hud_y + hud_height + 8),
            static_cast<float>(pickup_bar_width),
            8.0F,
            pickup_toast_color);
        PushText(
            scene,
            platform::RenderLayer::UI,
            4,
            static_cast<float>(hud_x + 18),
            static_cast<float>(hud_y + hud_height + 18),
            2.0F,
            "+" + std::to_string(player_state.pickup_toast_amount),
            platform::RgbaColor{.r = 255, .g = 255, .b = 255, .a = 240});
    }

    if (player_state.inventory_open) {
        const int inventory_panel_width = std::max(320, viewport_width / 2);
        const int inventory_panel_height = std::max(220, viewport_height / 3);
        const int inventory_x = (viewport_width - inventory_panel_width) / 2;
        const int inventory_y = (viewport_height - inventory_panel_height) / 2;
        PushFilledRect(
            scene,
            platform::RenderLayer::UI,
            100,
            static_cast<float>(inventory_x),
            static_cast<float>(inventory_y),
            static_cast<float>(inventory_panel_width),
            static_cast<float>(inventory_panel_height),
            platform::RgbaColor{.r = 22, .g = 22, .b = 28, .a = 228});

        const int craft_zone_margin = 14;
        PushFilledRect(
            scene,
            platform::RenderLayer::UI,
            101,
            static_cast<float>(inventory_x + craft_zone_margin),
            static_cast<float>(inventory_y + craft_zone_margin),
            static_cast<float>(inventory_panel_width / 3),
            static_cast<float>(inventory_panel_height - craft_zone_margin * 2),
            platform::RgbaColor{.r = 42, .g = 52, .b = 64, .a = 240});

        for (int recipe_index = 0; recipe_index < 3; ++recipe_index) {
            PushFilledRect(
                scene,
                platform::RenderLayer::UI,
                102,
                static_cast<float>(inventory_x + craft_zone_margin + 8),
                static_cast<float>(inventory_y + craft_zone_margin + 12 + recipe_index * 24),
                static_cast<float>(inventory_panel_width / 3 - 16),
                14.0F,
                recipe_index == static_cast<int>(player_state.selected_recipe_index)
                    ? platform::RgbaColor{.r = 218, .g = 188, .b = 96, .a = 240}
                    : platform::RgbaColor{.r = 72, .g = 82, .b = 94, .a = 220});
        }

        {
            const platform::RgbaColor title_color{.r = 232, .g = 232, .b = 236, .a = 240};
            PushText(
                scene,
                platform::RenderLayer::UI,
                200,
                static_cast<float>(inventory_x + craft_zone_margin + 10),
                static_cast<float>(inventory_y + craft_zone_margin - 10),
                2.0F,
                "CRAFT",
                title_color);
        }
    }

    return scene;
}

}  // namespace novaria::app
