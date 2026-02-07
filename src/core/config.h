#pragma once

#include <filesystem>
#include <string>

namespace novaria::core {

enum class ScriptBackendMode {
    LuaJit,
};

const char* ScriptBackendModeName(ScriptBackendMode mode);

enum class NetBackendMode {
    UdpLoopback,
};

const char* NetBackendModeName(NetBackendMode mode);

struct GameConfig final {
    std::string window_title = "Novaria";
    int window_width = 1280;
    int window_height = 720;
    bool vsync = true;
    bool strict_save_mod_fingerprint = false;
    ScriptBackendMode script_backend_mode = ScriptBackendMode::LuaJit;
    NetBackendMode net_backend_mode = NetBackendMode::UdpLoopback;
    std::string net_udp_local_host = "127.0.0.1";
    int net_udp_local_port = 0;
    std::string net_udp_remote_host = "127.0.0.1";
    int net_udp_remote_port = 0;
};

class ConfigLoader final {
public:
    static bool Load(
        const std::filesystem::path& file_path,
        GameConfig& out_config,
        std::string& out_error);
};

}  // namespace novaria::core
