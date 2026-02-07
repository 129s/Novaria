#include "core/config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

std::filesystem::path BuildTestDirectory() {
    return std::filesystem::temp_directory_path() / "novaria_config_loader_test";
}

bool WriteConfigFile(const std::filesystem::path& file_path, const std::string& content) {
    std::ofstream file(file_path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    file.close();
    return true;
}

}  // namespace

int main() {
    bool passed = true;

    const std::filesystem::path test_dir = BuildTestDirectory();
    const std::filesystem::path config_path = test_dir / "game.toml";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    std::filesystem::create_directories(test_dir, ec);

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTest\"\n"
            "window_width = 1600\n"
            "window_height = 900\n"
            "vsync = false\n"),
        "Config file write should succeed.");

    novaria::core::GameConfig default_config{};
    std::string error;
    passed &= Expect(
        novaria::core::ConfigLoader::Load(config_path, default_config, error),
        "Config load should succeed.");
    passed &= Expect(error.empty(), "Successful config load should not return error.");
    passed &= Expect(
        !default_config.strict_save_mod_fingerprint,
        "Strict fingerprint check should default to false.");
    passed &= Expect(
        default_config.script_backend_mode == novaria::core::ScriptBackendMode::LuaJit,
        "Script backend should default to luajit.");
    passed &= Expect(
        default_config.net_backend_mode == novaria::core::NetBackendMode::UdpLoopback,
        "Net backend should default to udp_loopback.");
    passed &= Expect(default_config.net_udp_local_port == 0, "Net UDP local port should default to 0.");
    passed &= Expect(
        default_config.net_udp_local_host == "127.0.0.1",
        "Net UDP local host should default to loopback.");
    passed &= Expect(
        default_config.net_udp_remote_host == "127.0.0.1",
        "Net UDP remote host should default to loopback.");
    passed &= Expect(default_config.net_udp_remote_port == 0, "Net UDP remote port should default to 0.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestStrict\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"luajit\"\n"
            "net_backend = \"udp_loopback\"\n"
            "net_udp_local_host = \"0.0.0.0\"\n"
            "net_udp_local_port = 24000\n"
            "net_udp_remote_host = \"127.0.0.1\"\n"
            "net_udp_remote_port = 24001\n"),
        "Strict config file write should succeed.");

    novaria::core::GameConfig strict_config{};
    passed &= Expect(
        novaria::core::ConfigLoader::Load(config_path, strict_config, error),
        "Config load should succeed with explicit backend keys.");
    passed &= Expect(error.empty(), "Strict config load should not return error.");
    passed &= Expect(
        strict_config.strict_save_mod_fingerprint,
        "Strict fingerprint check should parse as true.");
    passed &= Expect(
        strict_config.script_backend_mode == novaria::core::ScriptBackendMode::LuaJit,
        "Script backend should parse as luajit.");
    passed &= Expect(
        strict_config.net_backend_mode == novaria::core::NetBackendMode::UdpLoopback,
        "Net backend should parse as udp_loopback.");
    passed &= Expect(
        strict_config.net_udp_local_port == 24000 && strict_config.net_udp_remote_port == 24001,
        "Net UDP ports should parse correctly.");
    passed &= Expect(
        strict_config.net_udp_local_host == "0.0.0.0",
        "Net UDP local host should parse correctly.");
    passed &= Expect(
        strict_config.net_udp_remote_host == "127.0.0.1",
        "Net UDP remote host should parse correctly.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestInvalidScript\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"stub\"\n"
            "net_backend = \"udp_loopback\"\n"),
        "Invalid script backend config file write should succeed.");

    novaria::core::GameConfig invalid_script_config{};
    passed &= Expect(
        !novaria::core::ConfigLoader::Load(config_path, invalid_script_config, error),
        "Invalid script backend value should fail config load.");
    passed &= Expect(!error.empty(), "Invalid script backend should provide error.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestInvalidNet\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"luajit\"\n"
            "net_backend = \"stub\"\n"),
        "Invalid net backend config file write should succeed.");

    novaria::core::GameConfig invalid_net_config{};
    passed &= Expect(
        !novaria::core::ConfigLoader::Load(config_path, invalid_net_config, error),
        "Invalid net backend value should fail config load.");
    passed &= Expect(!error.empty(), "Invalid net backend should provide error.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestInvalidPort\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"luajit\"\n"
            "net_backend = \"udp_loopback\"\n"
            "net_udp_local_port = 70000\n"),
        "Invalid net UDP local port config file write should succeed.");

    novaria::core::GameConfig invalid_port_config{};
    passed &= Expect(
        !novaria::core::ConfigLoader::Load(config_path, invalid_port_config, error),
        "Out-of-range UDP local port should fail config load.");
    passed &= Expect(!error.empty(), "Out-of-range UDP local port should provide error.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestInvalidLocalHost\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"luajit\"\n"
            "net_backend = \"udp_loopback\"\n"
            "net_udp_local_host = \"\"\n"),
        "Invalid net UDP local host config file write should succeed.");

    novaria::core::GameConfig invalid_local_host_config{};
    passed &= Expect(
        !novaria::core::ConfigLoader::Load(config_path, invalid_local_host_config, error),
        "Empty UDP local host should fail config load.");
    passed &= Expect(!error.empty(), "Empty UDP local host should provide error.");

    std::filesystem::remove_all(test_dir, ec);

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_config_tests\n";
    return 0;
}
