#pragma once

#include "platform/input_actions.h"

namespace novaria::app {

struct PlayerInputIntent final {
    bool move_left = false;
    bool move_right = false;
    bool jump_pressed = false;
    bool cursor_valid = false;
    int cursor_screen_x = 0;
    int cursor_screen_y = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    bool action_primary_held = false;
    bool interaction_primary_pressed = false;
    bool hotbar_select_slot_1 = false;
    bool hotbar_select_slot_2 = false;
    bool hotbar_select_slot_3 = false;
    bool hotbar_select_slot_4 = false;
    bool hotbar_select_slot_5 = false;
    bool hotbar_select_slot_6 = false;
    bool hotbar_select_slot_7 = false;
    bool hotbar_select_slot_8 = false;
    bool hotbar_select_slot_9 = false;
    bool hotbar_select_slot_10 = false;
    bool hotbar_cycle_prev = false;
    bool hotbar_cycle_next = false;
    bool ui_inventory_toggle_pressed = false;
    bool hotbar_select_next_row = false;
    bool smart_mode_toggle_pressed = false;
    bool smart_context_held = false;

    bool ui_nav_up_pressed = false;
    bool ui_nav_down_pressed = false;
    bool ui_nav_confirm_pressed = false;
};

class InputCommandMapper final {
public:
    PlayerInputIntent Map(const platform::InputActions& frame_actions) const;
};

}  // namespace novaria::app
