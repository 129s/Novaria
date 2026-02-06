#pragma once

namespace novaria::platform {

struct InputActions final {
    bool send_jump_command = false;
    bool send_attack_command = false;
    bool emit_script_ping = false;
};

}  // namespace novaria::platform
