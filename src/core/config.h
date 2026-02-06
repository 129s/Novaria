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

enum class NetBackendMode {
    Auto,
    Stub,
    UdpLoopback,
};

const char* NetBackendModeName(NetBackendMode mode);

struct GameConfig final {
    std::string window_title = "Novaria";
    int window_width = 1280;
    int window_height = 720;
    bool vsync = true;
    bool strict_save_mod_fingerprint = false;
    ScriptBackendMode script_backend_mode = ScriptBackendMode::Auto;
    NetBackendMode net_backend_mode = NetBackendMode::Stub;
};

class ConfigLoader final {
public:
    static bool Load(
        const std::filesystem::path& file_path,
        GameConfig& out_config,
        std::string& out_error);
};

}  // namespace novaria::core
