#pragma once

#include "platform/input_actions.h"

namespace novaria::app {

struct PlayerInputIntent final {
    bool move_left = false;
    bool move_right = false;
    bool move_up = false;
    bool move_down = false;
    bool player_mine = false;
    bool player_place = false;
    bool build_workbench = false;
    bool craft_wood_sword = false;
    bool select_material_dirt = false;
    bool select_material_stone = false;
};

class InputCommandMapper final {
public:
    PlayerInputIntent Map(const platform::InputActions& frame_actions) const;
};

}  // namespace novaria::app
