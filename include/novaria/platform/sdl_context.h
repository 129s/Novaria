#pragma once

#include "core/config.h"
#include "platform/input_actions.h"
#include "platform/render_scene.h"

#include <SDL3/SDL.h>

namespace novaria::platform {

class SdlContext final {
public:
    ~SdlContext();

    bool Initialize(const core::GameConfig& config);
    bool PumpEvents(bool& quit_requested, InputActions& out_actions);
    void RenderFrame(float interpolation_alpha, const RenderScene& scene);
    void Shutdown();

private:
    bool debug_input_enabled_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
};

}  // namespace novaria::platform
