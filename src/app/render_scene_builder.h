#pragma once

#include "app/player_controller.h"
#include "core/config.h"
#include "platform/render_scene.h"
#include "world/world_service_basic.h"

namespace novaria::app {

class RenderSceneBuilder final {
public:
    platform::RenderScene Build(
        const LocalPlayerState& player_state,
        const core::GameConfig& config,
        const world::WorldServiceBasic& world_service,
        float daylight_factor) const;
};

}  // namespace novaria::app
