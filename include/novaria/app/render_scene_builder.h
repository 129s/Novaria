#pragma once

#include "app/player_controller.h"
#include "platform/render_scene.h"
#include "world/world_service.h"

namespace novaria::app {

class RenderSceneBuilder final {
public:
    platform::RenderScene Build(
        const LocalPlayerState& player_state,
        int viewport_width,
        int viewport_height,
        const world::IWorldService& world_service,
        float daylight_factor) const;
};

}  // namespace novaria::app
