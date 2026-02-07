#pragma once

#include <cstdint>
#include <vector>

namespace novaria::platform {

struct RenderTile final {
    int world_tile_x = 0;
    int world_tile_y = 0;
    std::uint16_t material_id = 0;
    std::uint8_t light_level = 255;
};

struct RenderHudState final {
    std::uint32_t dirt_count = 0;
    std::uint32_t stone_count = 0;
    std::uint32_t wood_count = 0;
    std::uint32_t coal_count = 0;
    std::uint32_t torch_count = 0;
    std::uint32_t workbench_count = 0;
    std::uint32_t wood_sword_count = 0;
    std::uint16_t pickup_toast_material_id = 0;
    std::uint32_t pickup_toast_amount = 0;
    std::uint16_t pickup_toast_ticks_remaining = 0;
    bool inventory_open = false;
    std::uint8_t selected_recipe_index = 0;
    std::uint16_t selected_material_id = 1;
    std::uint8_t hotbar_row = 0;
    std::uint8_t hotbar_slot = 0;
    bool workbench_built = false;
    bool wood_sword_crafted = false;
};

struct RenderScene final {
    int camera_tile_x = 0;
    int camera_tile_y = 0;
    int view_tiles_x = 0;
    int view_tiles_y = 0;
    int tile_pixel_size = 32;
    int player_tile_x = 0;
    int player_tile_y = 0;
    float daylight_factor = 1.0F;
    RenderHudState hud{};
    std::vector<RenderTile> tiles;
};

}  // namespace novaria::platform
