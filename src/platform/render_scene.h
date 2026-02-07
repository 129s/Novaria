#pragma once

#include <cstdint>
#include <vector>

namespace novaria::platform {

struct RenderTile final {
    int world_tile_x = 0;
    int world_tile_y = 0;
    std::uint16_t material_id = 0;
};

struct RenderHudState final {
    std::uint32_t dirt_count = 0;
    std::uint32_t stone_count = 0;
    std::uint16_t selected_material_id = 1;
};

struct RenderScene final {
    int camera_tile_x = 0;
    int camera_tile_y = 0;
    int view_tiles_x = 0;
    int view_tiles_y = 0;
    int tile_pixel_size = 32;
    int player_tile_x = 0;
    int player_tile_y = 0;
    RenderHudState hud{};
    std::vector<RenderTile> tiles;
};

}  // namespace novaria::platform
