#include "mod/mod_loader.h"

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
    return std::filesystem::temp_directory_path() / "novaria_mod_loader_test";
}

void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::trunc);
    file << content;
}

}  // namespace

int main() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    std::filesystem::create_directories(test_root / "mod_ok", ec);
    std::filesystem::create_directories(test_root / "mod_no_manifest", ec);

    WriteTextFile(
        test_root / "mod_ok" / "mod.toml",
        "name = \"mod_ok\"\n"
        "version = \"0.1.0\"\n"
        "description = \"A valid test mod\"\n");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(test_root, error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(loader.LoadAll(manifests, error), "LoadAll should succeed for valid manifests.");
    passed &= Expect(error.empty(), "LoadAll should not return error.");
    passed &= Expect(manifests.size() == 1, "Only one valid manifest should be loaded.");
    if (manifests.size() == 1) {
        passed &= Expect(manifests[0].name == "mod_ok", "Manifest name should match.");
        passed &= Expect(manifests[0].version == "0.1.0", "Manifest version should match.");
    }

    std::filesystem::create_directories(test_root / "mod_bad", ec);
    WriteTextFile(
        test_root / "mod_bad" / "mod.toml",
        "version = \"1.0.0\"\n");

    passed &= Expect(!loader.LoadAll(manifests, error), "LoadAll should fail when a manifest is invalid.");
    passed &= Expect(!error.empty(), "Invalid manifest should return error message.");

    loader.Shutdown();
    std::filesystem::remove_all(test_root, ec);

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_mod_loader_tests\n";
    return 0;
}
