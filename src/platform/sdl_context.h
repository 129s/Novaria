#pragma once

#include "core/config.h"

#include <SDL3/SDL.h>

namespace novaria::platform {

class SdlContext final {
public:
    ~SdlContext();

    bool Initialize(const core::GameConfig& config);
    bool PumpEvents(bool& quit_requested);
    void RenderFrame(float interpolation_alpha);
    void Shutdown();

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
};

}  // namespace novaria::platform
