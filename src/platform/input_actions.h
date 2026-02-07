#pragma once

namespace novaria::platform {

struct InputActions final {
    bool move_left = false;
    bool move_right = false;
    bool move_up = false;
    bool move_down = false;
    bool player_mine = false;
    bool player_place = false;
    bool select_material_dirt = false;
    bool select_material_stone = false;

    bool send_jump_command = false;
    bool send_attack_command = false;
    bool emit_script_ping = false;
    bool debug_set_tile_air = false;
    bool debug_set_tile_stone = false;
    bool debug_net_disconnect = false;
    bool debug_net_heartbeat = false;
    bool debug_net_connect = false;
    bool gameplay_collect_wood = false;
    bool gameplay_collect_stone = false;
    bool gameplay_build_workbench = false;
    bool gameplay_craft_sword = false;
    bool gameplay_attack_enemy = false;
    bool gameplay_attack_boss = false;
};

}  // namespace novaria::platform
