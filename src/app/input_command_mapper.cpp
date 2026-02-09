#include "app/input_command_mapper.h"

namespace novaria::app {

PlayerInputIntent InputCommandMapper::Map(const platform::InputActions& frame_actions) const {
    PlayerInputIntent intent{
        .move_left = frame_actions.move_left,
        .move_right = frame_actions.move_right,
        .jump_pressed = frame_actions.jump_pressed,
        .cursor_valid = frame_actions.cursor_valid,
        .cursor_screen_x = frame_actions.cursor_screen_x,
        .cursor_screen_y = frame_actions.cursor_screen_y,
        .viewport_width = frame_actions.viewport_width,
        .viewport_height = frame_actions.viewport_height,
        .action_primary_held = frame_actions.action_primary_held,
        .interaction_primary_pressed = frame_actions.interaction_primary_pressed,
        .hotbar_select_slot_1 = frame_actions.hotbar_select_slot_1,
        .hotbar_select_slot_2 = frame_actions.hotbar_select_slot_2,
        .hotbar_select_slot_3 = frame_actions.hotbar_select_slot_3,
        .hotbar_select_slot_4 = frame_actions.hotbar_select_slot_4,
        .hotbar_select_slot_5 = frame_actions.hotbar_select_slot_5,
        .hotbar_select_slot_6 = frame_actions.hotbar_select_slot_6,
        .hotbar_select_slot_7 = frame_actions.hotbar_select_slot_7,
        .hotbar_select_slot_8 = frame_actions.hotbar_select_slot_8,
        .hotbar_select_slot_9 = frame_actions.hotbar_select_slot_9,
        .hotbar_select_slot_10 = frame_actions.hotbar_select_slot_10,
        .hotbar_cycle_prev = frame_actions.hotbar_cycle_prev,
        .hotbar_cycle_next = frame_actions.hotbar_cycle_next,
        .ui_inventory_toggle_pressed = frame_actions.ui_inventory_toggle_pressed,
        .hotbar_select_next_row = frame_actions.hotbar_select_next_row,
        .smart_mode_toggle_pressed = frame_actions.smart_mode_toggle_pressed,
        .smart_context_held = frame_actions.smart_context_held,
        .ui_nav_up_pressed = frame_actions.ui_nav_up_pressed,
        .ui_nav_down_pressed = frame_actions.ui_nav_down_pressed,
        .ui_nav_confirm_pressed = frame_actions.ui_nav_confirm_pressed,
    };

    if (intent.move_left && intent.move_right) {
        intent.move_left = false;
        intent.move_right = false;
    }

    return intent;
}

}  // namespace novaria::app
