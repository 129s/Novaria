#pragma once

#include <filesystem>
#include <string>

namespace novaria::core {

struct GameConfig final {
    std::string window_title = "Novaria";
    int window_width = 1280;
    int window_height = 720;
    bool vsync = true;
};

class ConfigLoader final {
public:
    static bool Load(
        const std::filesystem::path& file_path,
        GameConfig& out_config,
        std::string& out_error);
};

}  // namespace novaria::core
