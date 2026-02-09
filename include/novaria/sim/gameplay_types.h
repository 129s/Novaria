#pragma once

#include <cstdint>

namespace novaria::sim {

struct GameplayProgressSnapshot final {
    std::uint32_t wood_collected = 0;
    std::uint32_t stone_collected = 0;
    bool workbench_built = false;
    bool sword_crafted = false;
    std::uint32_t enemy_kill_count = 0;
    std::uint32_t boss_health = 0;
    bool boss_defeated = false;
    bool playable_loop_complete = false;
};

struct GameplayPickupEvent final {
    std::uint32_t player_id = 0;
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    std::uint16_t resource_id = 0;
    std::uint32_t amount = 0;
};

struct PlayerInventorySnapshot final {
    std::uint32_t dirt_count = 0;
    std::uint32_t stone_count = 0;
    std::uint32_t wood_count = 0;
    std::uint32_t coal_count = 0;
    std::uint32_t torch_count = 0;
    std::uint32_t workbench_count = 0;
    std::uint32_t wood_sword_count = 0;
    bool has_pickaxe_tool = true;
    bool has_axe_tool = true;
};

}  // namespace novaria::sim
