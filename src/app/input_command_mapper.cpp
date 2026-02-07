#include "app/input_command_mapper.h"

namespace novaria::app {

PlayerInputIntent InputCommandMapper::Map(const platform::InputActions& frame_actions) const {
    PlayerInputIntent intent{
        .move_left = frame_actions.move_left,
        .move_right = frame_actions.move_right,
        .move_up = frame_actions.move_up,
        .move_down = frame_actions.move_down,
        .action_primary_held = frame_actions.action_primary_held,
        .interaction_primary_pressed = frame_actions.interaction_primary_pressed,
        .hotbar_select_slot_1 = frame_actions.hotbar_select_slot_1,
        .hotbar_select_slot_2 = frame_actions.hotbar_select_slot_2,
        .hotbar_select_slot_3 = frame_actions.hotbar_select_slot_3,
        .hotbar_select_slot_4 = frame_actions.hotbar_select_slot_4,
        .ui_inventory_toggle_pressed = frame_actions.ui_inventory_toggle_pressed,
        .hotbar_select_next_row = frame_actions.hotbar_select_next_row,
        .smart_mode_toggle_pressed = frame_actions.smart_mode_toggle_pressed,
        .smart_context_held = frame_actions.smart_context_held,
    };

    if (intent.move_left && intent.move_right) {
        intent.move_left = false;
        intent.move_right = false;
    }
    if (intent.move_up && intent.move_down) {
        intent.move_up = false;
        intent.move_down = false;
    }

    return intent;
}

}  // namespace novaria::app
