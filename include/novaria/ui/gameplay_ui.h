#pragma once

#include "platform/render_scene.h"
#include "sim/gameplay_types.h"

#include <cstdint>
#include <string>

namespace novaria::ui {

struct GameplayUiPalette final {
    platform::RgbaColor text{};
    platform::RgbaColor text_muted{};
    platform::RgbaColor panel{};
    platform::RgbaColor panel_2{};
    platform::RgbaColor border{};
    platform::RgbaColor accent{};
    platform::RgbaColor danger{};
    platform::RgbaColor success{};

    platform::RgbaColor dirt{};
    platform::RgbaColor stone{};
    platform::RgbaColor wood{};
    platform::RgbaColor coal{};
    platform::RgbaColor torch{};
    platform::RgbaColor workbench{};
};

struct GameplayUiFrameContext final {
    int viewport_width = 0;
    int viewport_height = 0;
    float world_origin_x = 0.0F;
    float world_origin_y = 0.0F;
    int tile_pixel_size = 32;
};

struct GameplayUiModel final {
    std::uint16_t hp_current = 0;
    std::uint16_t hp_max = 0;

    std::uint32_t dirt_count = 0;
    std::uint32_t stone_count = 0;
    std::uint32_t wood_count = 0;
    std::uint32_t coal_count = 0;
    std::uint32_t torch_count = 0;
    std::uint32_t workbench_count = 0;
    std::uint32_t wood_sword_count = 0;

    bool workbench_built = false;
    bool wood_sword_crafted = false;

    std::uint8_t hotbar_rows = 2;
    std::uint8_t active_hotbar_row = 0;
    std::uint8_t selected_hotbar_slot = 0;

    bool smart_mode_enabled = false;
    bool context_slot_visible = false;
    std::uint8_t context_slot_current = 0;

    bool target_highlight_visible = false;
    int target_tile_x = 0;
    int target_tile_y = 0;
    bool target_reachable = false;

    std::uint16_t target_material_id = 0;
    std::uint16_t selected_place_material_id = 0;

    bool inventory_open = false;
    std::uint8_t selected_recipe_index = 0;
    bool workbench_in_range = false;

    std::uint16_t pickup_toast_material_id = 0;
    std::uint32_t pickup_toast_amount = 0;
    std::uint16_t pickup_toast_ticks_remaining = 0;
    std::string pickup_toast_label;

    std::uint8_t last_interaction_type = 0;
    std::uint16_t last_interaction_ticks_remaining = 0;

    sim::ActionPrimaryProgressSnapshot action_primary_progress{};
};

void AppendGameplayUi(
    platform::RenderScene& scene,
    const GameplayUiFrameContext& frame,
    const GameplayUiPalette& palette,
    const GameplayUiModel& model);

}  // namespace novaria::ui

