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

    novaria::core::GameConfig missing_strict_config{};
    std::string error;
    passed &= Expect(
        novaria::core::ConfigLoader::Load(config_path, missing_strict_config, error),
        "Config load should succeed without strict key.");
    passed &= Expect(error.empty(), "Successful config load should not return error.");
    passed &= Expect(
        !missing_strict_config.strict_save_mod_fingerprint,
        "Strict fingerprint check should default to false.");
    passed &= Expect(
        missing_strict_config.script_backend_mode == novaria::core::ScriptBackendMode::Auto,
        "Script backend should default to auto.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestStrict\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"stub\"\n"),
        "Strict config file write should succeed.");

    novaria::core::GameConfig strict_config{};
    passed &= Expect(
        novaria::core::ConfigLoader::Load(config_path, strict_config, error),
        "Config load should succeed with strict key.");
    passed &= Expect(error.empty(), "Strict config load should not return error.");
    passed &= Expect(
        strict_config.strict_save_mod_fingerprint,
        "Strict fingerprint check should parse as true.");
    passed &= Expect(
        strict_config.script_backend_mode == novaria::core::ScriptBackendMode::Stub,
        "Script backend should parse as stub.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestInvalid\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"luajit\"\n"),
        "Invalid config file write should succeed.");

    novaria::core::GameConfig luajit_config{};
    passed &= Expect(
        novaria::core::ConfigLoader::Load(config_path, luajit_config, error),
        "LuaJIT script backend value should parse.");
    passed &= Expect(
        luajit_config.script_backend_mode == novaria::core::ScriptBackendMode::LuaJit,
        "Script backend should parse as luajit.");

    passed &= Expect(
        WriteConfigFile(
            config_path,
            "window_title = \"CfgTestInvalid\"\n"
            "window_width = 1280\n"
            "window_height = 720\n"
            "vsync = true\n"
            "strict_save_mod_fingerprint = true\n"
            "script_backend = \"invalid\"\n"),
        "Invalid config file write should succeed.");

    novaria::core::GameConfig invalid_config{};
    passed &= Expect(
        !novaria::core::ConfigLoader::Load(config_path, invalid_config, error),
        "Invalid script backend value should fail config load.");
    passed &= Expect(!error.empty(), "Invalid config load should provide error.");

    std::filesystem::remove_all(test_dir, ec);

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_config_tests\n";
    return 0;
}
