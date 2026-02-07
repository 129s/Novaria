#pragma once

#include "app/input_command_mapper.h"
#include "sim/simulation_kernel.h"
#include "world/world_service_basic.h"

#include <cstdint>

namespace novaria::app {

struct LocalPlayerState final {
    int tile_x = 0;
    int tile_y = -1;
    int facing_x = 1;
    std::uint32_t inventory_dirt_count = 0;
    std::uint32_t inventory_stone_count = 0;
    std::uint16_t selected_place_material_id = 1;
    bool loaded_chunk_window_ready = false;
    int loaded_chunk_window_center_x = 0;
    int loaded_chunk_window_center_y = 0;
};

class PlayerController final {
public:
    void Reset();
    const LocalPlayerState& State() const;
    void Update(
        const PlayerInputIntent& input_intent,
        world::WorldServiceBasic& world_service,
        sim::SimulationKernel& simulation_kernel,
        std::uint32_t local_player_id);

private:
    static int FloorDiv(int value, int divisor);
    static world::ChunkCoord TileToChunkCoord(int tile_x, int tile_y);
    static bool IsSolidMaterial(std::uint16_t material_id);
    static bool IsCollectibleMaterial(std::uint16_t material_id);

    LocalPlayerState state_{};
};

}  // namespace novaria::app
