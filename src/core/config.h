#pragma once

#include <filesystem>
#include <string>

namespace novaria::core {

enum class ScriptBackendMode {
    Auto,
    Stub,
    LuaJit,
};

const char* ScriptBackendModeName(ScriptBackendMode mode);

struct GameConfig final {
    std::string window_title = "Novaria";
    int window_width = 1280;
    int window_height = 720;
    bool vsync = true;
    bool strict_save_mod_fingerprint = false;
    ScriptBackendMode script_backend_mode = ScriptBackendMode::Auto;
};

class ConfigLoader final {
public:
    static bool Load(
        const std::filesystem::path& file_path,
        GameConfig& out_config,
        std::string& out_error);
};

}  // namespace novaria::core
