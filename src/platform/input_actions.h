#pragma once

namespace novaria::platform {

struct InputActions final {
    bool send_jump_command = false;
    bool send_attack_command = false;
    bool emit_script_ping = false;
    bool debug_set_tile_air = false;
    bool debug_set_tile_stone = false;
    bool debug_net_disconnect = false;
    bool debug_net_heartbeat = false;
    bool debug_net_connect = false;
};

}  // namespace novaria::platform
