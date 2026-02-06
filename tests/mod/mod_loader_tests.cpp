#include "mod/mod_loader.h"

#include <algorithm>
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

bool TestLoadAllAndFingerprint() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);

    std::filesystem::create_directories(test_root / "mod_ok_a", ec);
    std::filesystem::create_directories(test_root / "mod_ok_b", ec);
    std::filesystem::create_directories(test_root / "mod_no_manifest", ec);

    WriteTextFile(
        test_root / "mod_ok_a" / "mod.toml",
        "name = \"mod_ok_a\"\n"
        "version = \"0.1.0\"\n"
        "description = \"A valid test mod\"\n");
    WriteTextFile(
        test_root / "mod_ok_b" / "mod.toml",
        "name = \"mod_ok_b\"\n"
        "version = \"0.2.0\"\n"
        "description = \"Another valid test mod\"\n");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(test_root, error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(loader.LoadAll(manifests, error), "LoadAll should succeed for valid manifests.");
    passed &= Expect(error.empty(), "LoadAll should not return error.");
    passed &= Expect(manifests.size() == 2, "Two valid manifests should be loaded.");

    const std::string fingerprint_a = novaria::mod::ModLoader::BuildManifestFingerprint(manifests);
    passed &= Expect(!fingerprint_a.empty(), "Manifest fingerprint should not be empty.");

    std::vector<novaria::mod::ModManifest> reordered = manifests;
    std::reverse(reordered.begin(), reordered.end());
    const std::string fingerprint_b = novaria::mod::ModLoader::BuildManifestFingerprint(reordered);
    passed &= Expect(
        fingerprint_a == fingerprint_b,
        "Manifest fingerprint should be order-insensitive.");

    reordered[0].version = "9.9.9";
    const std::string fingerprint_c = novaria::mod::ModLoader::BuildManifestFingerprint(reordered);
    passed &= Expect(
        fingerprint_c != fingerprint_a,
        "Manifest fingerprint should change when manifest content changes.");

    loader.Shutdown();
    std::filesystem::remove_all(test_root, ec);
    return passed;
}

bool TestRejectInvalidManifest() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);

    std::filesystem::create_directories(test_root / "mod_bad", ec);
    WriteTextFile(
        test_root / "mod_bad" / "mod.toml",
        "version = \"1.0.0\"\n");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(test_root, error), "Initialize should succeed for test root.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(!loader.LoadAll(manifests, error), "LoadAll should fail when manifest is invalid.");
    passed &= Expect(!error.empty(), "Invalid manifest should return error message.");

    loader.Shutdown();
    std::filesystem::remove_all(test_root, ec);
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestLoadAllAndFingerprint();
    passed &= TestRejectInvalidManifest();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_mod_loader_tests\n";
    return 0;
}
