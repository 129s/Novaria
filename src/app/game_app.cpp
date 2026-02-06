#include "app/game_app.h"

#include "app/game_loop.h"
#include "core/logger.h"

#include <string>

namespace novaria::app {

bool GameApp::Initialize(const std::filesystem::path& config_path) {
    std::string config_error;
    if (!core::ConfigLoader::Load(config_path, config_, config_error)) {
        core::Logger::Warn("config", "Config load failed, using defaults: " + config_error);
    } else {
        core::Logger::Info("config", "Config loaded: " + config_path.string());
    }

    if (!sdl_context_.Initialize(config_)) {
        core::Logger::Error("app", "SDL3 initialization failed.");
        return false;
    }

    initialized_ = true;
    core::Logger::Info("app", "Novaria started.");
    return true;
}

int GameApp::Run() {
    if (!initialized_) {
        core::Logger::Error("app", "Run called before initialization.");
        return 1;
    }

    GameLoop loop;
    loop.Run(
        [this]() -> bool {
            if (!sdl_context_.PumpEvents(quit_requested_)) {
                core::Logger::Error("platform", "Event pump failed.");
                return false;
            }
            return !quit_requested_;
        },
        [](double) {},
        [this](float interpolation_alpha) { sdl_context_.RenderFrame(interpolation_alpha); });

    core::Logger::Info("app", "Main loop exited.");
    return 0;
}

void GameApp::Shutdown() {
    if (!initialized_) {
        return;
    }

    sdl_context_.Shutdown();
    initialized_ = false;
    core::Logger::Info("app", "Novaria shutdown complete.");
}

}  // namespace novaria::app
