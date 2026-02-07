#include "app/input_command_mapper.h"

namespace novaria::app {

PlayerInputIntent InputCommandMapper::Map(const platform::InputActions& frame_actions) const {
    PlayerInputIntent intent{
        .move_left = frame_actions.move_left,
        .move_right = frame_actions.move_right,
        .move_up = frame_actions.move_up,
        .move_down = frame_actions.move_down,
        .player_mine = frame_actions.player_mine,
        .player_place = frame_actions.player_place,
        .build_workbench = frame_actions.build_workbench,
        .craft_wood_sword = frame_actions.craft_wood_sword,
        .select_material_dirt = frame_actions.select_material_dirt,
        .select_material_stone = frame_actions.select_material_stone,
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
