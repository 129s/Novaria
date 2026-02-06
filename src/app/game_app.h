#pragma once

#include "core/config.h"
#include "platform/sdl_context.h"

#include <filesystem>

namespace novaria::app {

class GameApp final {
public:
    bool Initialize(const std::filesystem::path& config_path);
    int Run();
    void Shutdown();

private:
    bool initialized_ = false;
    bool quit_requested_ = false;
    core::GameConfig config_;
    platform::SdlContext sdl_context_;
};

}  // namespace novaria::app
