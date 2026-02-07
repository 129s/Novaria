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
};

}  // namespace novaria::platform
