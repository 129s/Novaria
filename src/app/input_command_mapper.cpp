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
        .select_material_dirt = frame_actions.select_material_dirt,
        .select_material_stone = frame_actions.select_material_stone,
        .send_jump_command = frame_actions.send_jump_command,
        .fire_projectile = frame_actions.send_attack_command,
        .emit_script_ping = frame_actions.emit_script_ping,
        .debug_set_tile_air = frame_actions.debug_set_tile_air,
        .debug_set_tile_stone = frame_actions.debug_set_tile_stone,
        .gameplay_collect_wood = frame_actions.gameplay_collect_wood,
        .gameplay_collect_stone = frame_actions.gameplay_collect_stone,
        .gameplay_build_workbench = frame_actions.gameplay_build_workbench,
        .gameplay_craft_sword = frame_actions.gameplay_craft_sword,
        .gameplay_attack_enemy = frame_actions.gameplay_attack_enemy,
        .gameplay_attack_boss = frame_actions.gameplay_attack_boss,
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
