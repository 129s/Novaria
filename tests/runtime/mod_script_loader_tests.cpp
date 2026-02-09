#include "runtime/mod_script_loader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

std::filesystem::path BuildTestDirectory() {
    const auto unique_seed =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("novaria_mod_script_loader_test_" + std::to_string(unique_seed));
}

void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
}

novaria::mod::ModManifest BuildManifest(
    const std::filesystem::path& root_path,
    std::string script_entry) {
    novaria::mod::ModManifest manifest{};
    manifest.name = "mod_path_guard";
    manifest.version = "0.1.0";
    manifest.container_kind = novaria::mod::ModContainerKind::Directory;
    manifest.container_path = root_path;
    manifest.script_entry = std::move(script_entry);
    return manifest;
}

bool TestLoadValidScriptEntry() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    const std::filesystem::path mod_root = test_root / "mod_ok";
    WriteTextFile(mod_root / "content" / "scripts" / "main.lua", "novaria = novaria or {}\n");

    std::vector<novaria::script::ScriptModuleSource> modules;
    std::string error;
    passed &= Expect(
        novaria::runtime::BuildModScriptModules(
            {BuildManifest(mod_root, "content/scripts/main.lua")},
            modules,
            error),
        "Valid script_entry should load script module source.");
    passed &= Expect(error.empty(), "Valid script_entry should not return error.");
    passed &= Expect(modules.size() == 1, "Valid script_entry should produce one module.");

    std::filesystem::remove_all(test_root, ec);
    return passed;
}

bool TestRejectPathTraversalScriptEntry() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    const std::filesystem::path mod_root = test_root / "mod_bad";
    WriteTextFile(mod_root / "content" / "scripts" / "main.lua", "return 1\n");

    std::vector<novaria::script::ScriptModuleSource> modules;
    std::string error;
    passed &= Expect(
        !novaria::runtime::BuildModScriptModules(
            {BuildManifest(mod_root, "../outside.lua")},
            modules,
            error),
        "Path traversal script_entry should be rejected.");
    passed &= Expect(
        !error.empty() && error.find("Invalid script entry path") != std::string::npos,
        "Traversal rejection should return readable invalid-path error.");

    std::filesystem::remove_all(test_root, ec);
    return passed;
}

bool TestRejectRootNameScriptEntryOnWindows() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    const std::filesystem::path mod_root = test_root / "mod_bad_root_name";
    WriteTextFile(mod_root / "content" / "scripts" / "main.lua", "return 1\n");

    std::vector<novaria::script::ScriptModuleSource> modules;
    std::string error;
#if defined(_WIN32)
    passed &= Expect(
        !novaria::runtime::BuildModScriptModules(
            {BuildManifest(mod_root, "C:escape.lua")},
            modules,
            error),
        "Windows root-name script_entry should be rejected.");
    passed &= Expect(
        !error.empty() && error.find("Invalid script entry path") != std::string::npos,
        "Windows root-name rejection should return readable invalid-path error.");
#else
    passed &= Expect(
        novaria::runtime::BuildModScriptModules(
            {BuildManifest(mod_root, "C:escape.lua")},
            modules,
            error),
        "Non-Windows path semantics should treat C:prefix as regular relative path.");
#endif

    std::filesystem::remove_all(test_root, ec);
    return passed;
}

bool TestRejectAbsoluteScriptEntry() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    const std::filesystem::path mod_root = test_root / "mod_bad_abs";
    WriteTextFile(mod_root / "content" / "scripts" / "main.lua", "return 1\n");

    std::vector<novaria::script::ScriptModuleSource> modules;
    std::string error;
    const std::filesystem::path absolute_script =
        mod_root / "content" / "scripts" / "main.lua";
    passed &= Expect(
        !novaria::runtime::BuildModScriptModules(
            {BuildManifest(mod_root, absolute_script.string())},
            modules,
            error),
        "Absolute script_entry should be rejected.");
    passed &= Expect(
        !error.empty() && error.find("Invalid script entry path") != std::string::npos,
        "Absolute path rejection should return readable invalid-path error.");

    std::filesystem::remove_all(test_root, ec);
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestLoadValidScriptEntry();
    passed &= TestRejectPathTraversalScriptEntry();
    passed &= TestRejectRootNameScriptEntryOnWindows();
    passed &= TestRejectAbsoluteScriptEntry();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_mod_script_loader_tests\n";
    return 0;
}
