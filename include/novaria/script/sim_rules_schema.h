#pragma once

#include <cstdint>

namespace novaria::script::simrpc {

inline constexpr std::uint8_t kVersion = 1;

enum class Command : std::uint8_t {
    Validate = 0,
    GameplayActionPrimary = 1,
    GameplayCraftRecipe = 2,
};

enum class ActionPrimaryResult : std::uint8_t {
    Reject = 0,
    Harvest = 1,
    Place = 2,
};

enum class PlaceKind : std::uint8_t {
    None = 0,
    Dirt = 1,
    Stone = 2,
    Torch = 3,
    Workbench = 4,
};

enum class CraftRecipeResult : std::uint8_t {
    Reject = 0,
    Craft = 1,
};

enum class CraftedKind : std::uint8_t {
    None = 0,
    Workbench = 1,
    Torch = 2,
};

struct ValidateResponse final {
    bool ok = false;
};

struct ActionPrimaryRequest final {
    std::uint32_t player_id = 0;
    int player_tile_x = 0;
    int player_tile_y = 0;
    int target_tile_x = 0;
    int target_tile_y = 0;
    std::uint8_t hotbar_row = 0;
    std::uint8_t hotbar_slot = 0;

    std::uint32_t dirt_count = 0;
    std::uint32_t stone_count = 0;
    std::uint32_t wood_count = 0;
    std::uint32_t coal_count = 0;
    std::uint32_t torch_count = 0;
    std::uint32_t workbench_count = 0;
    std::uint32_t wood_sword_count = 0;
    bool has_pickaxe_tool = false;
    bool has_axe_tool = false;

    bool target_is_air = false;
    std::uint32_t harvest_ticks = 0;
    bool harvestable_by_pickaxe = false;
    bool harvestable_by_axe = false;
    bool harvestable_by_sword = false;
};

struct ActionPrimaryResponse final {
    ActionPrimaryResult result = ActionPrimaryResult::Reject;
    PlaceKind place_kind = PlaceKind::None;
    std::uint32_t required_ticks = 0;
};

struct CraftRecipeRequest final {
    std::uint32_t player_id = 0;
    int player_tile_x = 0;
    int player_tile_y = 0;
    std::uint8_t recipe_index = 0;
    bool workbench_reachable = false;

    std::uint32_t dirt_count = 0;
    std::uint32_t stone_count = 0;
    std::uint32_t wood_count = 0;
    std::uint32_t coal_count = 0;
    std::uint32_t torch_count = 0;
    std::uint32_t workbench_count = 0;
    std::uint32_t wood_sword_count = 0;
};

struct CraftRecipeResponse final {
    CraftRecipeResult result = CraftRecipeResult::Reject;
    int dirt_delta = 0;
    int stone_delta = 0;
    int wood_delta = 0;
    int coal_delta = 0;
    int torch_delta = 0;
    int workbench_delta = 0;
    int wood_sword_delta = 0;
    CraftedKind crafted_kind = CraftedKind::None;
    bool mark_workbench_built = false;
    bool mark_sword_crafted = false;
};

}  // namespace novaria::script::simrpc

