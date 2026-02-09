#pragma once

#include <filesystem>
#include <string>

namespace novaria::core {

struct GameConfig final {
    std::string window_title = "Novaria";
    int window_width = 1280;
    int window_height = 720;
    bool vsync = true;
    bool debug_input_enabled = false;
    bool strict_save_mod_fingerprint = false;
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
