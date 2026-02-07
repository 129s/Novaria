#pragma once

#include "platform/input_actions.h"

namespace novaria::app {

struct PlayerInputIntent final {
    bool move_left = false;
    bool move_right = false;
    bool move_up = false;
    bool move_down = false;
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

class InputCommandMapper final {
public:
    PlayerInputIntent Map(const platform::InputActions& frame_actions) const;
};

}  // namespace novaria::app
