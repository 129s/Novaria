#include "app/render_scene_builder.h"

#include <algorithm>

namespace novaria::app {

platform::RenderScene RenderSceneBuilder::Build(
    const LocalPlayerState& player_state,
    const core::GameConfig& config,
    const world::WorldServiceBasic& world_service) const {
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
        .selected_material_id = player_state.selected_place_material_id,
    };

    const int first_world_tile_x = scene.camera_tile_x - scene.view_tiles_x / 2;
    const int first_world_tile_y = scene.camera_tile_y - scene.view_tiles_y / 2;
    scene.tiles.reserve(static_cast<std::size_t>(scene.view_tiles_x * scene.view_tiles_y));
    for (int local_y = 0; local_y < scene.view_tiles_y; ++local_y) {
        for (int local_x = 0; local_x < scene.view_tiles_x; ++local_x) {
            const int world_tile_x = first_world_tile_x + local_x;
            const int world_tile_y = first_world_tile_y + local_y;
            std::uint16_t material_id = 0;
            (void)world_service.TryReadTile(
                world_tile_x,
                world_tile_y,
                material_id);

            scene.tiles.push_back(platform::RenderTile{
                .world_tile_x = world_tile_x,
                .world_tile_y = world_tile_y,
                .material_id = material_id,
            });
        }
    }

    return scene;
}

}  // namespace novaria::app
