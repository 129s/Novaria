#pragma once

namespace novaria::platform {

struct InputActions final {
    bool move_left = false;
    bool move_right = false;
    bool jump_pressed = false;
    bool action_primary_held = false;
    bool interaction_primary_pressed = false;
    bool hotbar_select_slot_1 = false;
    bool hotbar_select_slot_2 = false;
    bool hotbar_select_slot_3 = false;
    bool hotbar_select_slot_4 = false;
    bool ui_inventory_toggle_pressed = false;
    bool hotbar_select_next_row = false;
    bool smart_mode_toggle_pressed = false;
    bool smart_context_held = false;
};

}  // namespace novaria::platform
