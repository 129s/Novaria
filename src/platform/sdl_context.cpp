#include "platform/sdl_context.h"

#include "core/logger.h"

#include <cmath>
#include <string>

namespace novaria::platform {
namespace {

std::string SdlError() {
    const char* error = SDL_GetError();
    return error == nullptr ? std::string("unknown error") : std::string(error);
}

bool IsQuitEvent(Uint32 event_type) {
    (void)event_type;

#if defined(SDL_EVENT_QUIT)
    if (event_type == SDL_EVENT_QUIT) {
        return true;
    }
#endif

#if defined(SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    if (event_type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return true;
    }
#endif

    return false;
}

}  // namespace

SdlContext::~SdlContext() {
    Shutdown();
}

bool SdlContext::Initialize(const core::GameConfig& config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        core::Logger::Error("platform", "SDL_Init failed: " + SdlError());
        return false;
    }

    window_ = SDL_CreateWindow(
        config.window_title.c_str(),
        config.window_width,
        config.window_height,
        SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        core::Logger::Error("platform", "SDL_CreateWindow failed: " + SdlError());
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
        core::Logger::Error("platform", "SDL_CreateRenderer failed: " + SdlError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }

    const int vsync = config.vsync ? 1 : 0;
    (void)SDL_SetRenderVSync(renderer_, vsync);

    core::Logger::Info("platform", "SDL3 context initialized.");
    return true;
}

bool SdlContext::PumpEvents(bool& quit_requested) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (IsQuitEvent(event.type)) {
            quit_requested = true;
        }
    }
    return true;
}

void SdlContext::RenderFrame(float interpolation_alpha) {
    if (renderer_ == nullptr) {
        return;
    }

    const float seconds = static_cast<float>(SDL_GetTicks()) / 1000.0F;
    const float pulse = (std::sin(seconds * 2.0F) + 1.0F) * 0.5F;
    const Uint8 blue = static_cast<Uint8>(80 + static_cast<int>(pulse * 100.0F));
    const Uint8 green = static_cast<Uint8>(20 + static_cast<int>(interpolation_alpha * 30.0F));

    (void)SDL_SetRenderDrawColor(renderer_, 24, green, blue, 255);
    (void)SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
}

void SdlContext::Shutdown() {
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Quit();
    }
}

}  // namespace novaria::platform
