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
    std::filesystem::create_directories(test_root / "mod_ok_a" / "content", ec);
    std::filesystem::create_directories(test_root / "mod_ok_b" / "content", ec);

    WriteTextFile(
        test_root / "mod_ok_a" / "mod.toml",
        "name = \"mod_ok_a\"\n"
        "version = \"0.1.0\"\n"
        "description = \"A valid test mod\"\n"
        "dependencies = []\n"
        "script_entry = \"content/scripts/core.lua\"\n"
        "script_api_version = \"0.1.0\"\n");
    WriteTextFile(
        test_root / "mod_ok_a" / "content" / "items.csv",
        "wood_pickaxe,tool.mine_speed+1\n");
    WriteTextFile(
        test_root / "mod_ok_a" / "content" / "recipes.csv",
        "recipe_pickaxe,wood_pickaxe,1,wood_pickaxe_plus,1\n");
    WriteTextFile(
        test_root / "mod_ok_b" / "mod.toml",
        "name = \"mod_ok_b\"\n"
        "version = \"0.2.0\"\n"
        "description = \"Another valid test mod\"\n"
        "dependencies = [\"mod_ok_a\"]\n");
    WriteTextFile(
        test_root / "mod_ok_b" / "content" / "npcs.csv",
        "slime_boss,200,boss.jump_charge\n");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(test_root, error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(loader.LoadAll(manifests, error), "LoadAll should succeed for valid manifests.");
    passed &= Expect(error.empty(), "LoadAll should not return error.");
    passed &= Expect(manifests.size() == 2, "Two valid manifests should be loaded.");
    passed &= Expect(
        manifests[0].name == "mod_ok_a" && manifests[1].name == "mod_ok_b",
        "Manifest load order should follow dependency topology.");
    passed &= Expect(
        manifests[1].dependencies.size() == 1 && manifests[1].dependencies[0] == "mod_ok_a",
        "Manifest dependencies should parse from TOML array.");
    passed &= Expect(
        manifests[0].items.size() == 1 && manifests[0].recipes.size() == 1,
        "Mod content loader should parse item and recipe definitions.");
    passed &= Expect(
        manifests[1].npcs.size() == 1 && manifests[1].npcs[0].behavior == "boss.jump_charge",
        "Mod content loader should parse npc definitions.");
    passed &= Expect(
        manifests[0].script_entry == "content/scripts/core.lua" &&
            manifests[0].script_api_version == "0.1.0",
        "Mod manifest should parse optional script metadata.");

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

    reordered[0].version = manifests[1].version;
    reordered[0].dependencies.clear();
    const std::string fingerprint_d = novaria::mod::ModLoader::BuildManifestFingerprint(reordered);
    passed &= Expect(
        fingerprint_d != fingerprint_a,
        "Manifest fingerprint should change when dependency content changes.");

    reordered[0].dependencies = manifests[1].dependencies;
    reordered[0].npcs[0].behavior = "boss.frenzy";
    const std::string fingerprint_e = novaria::mod::ModLoader::BuildManifestFingerprint(reordered);
    passed &= Expect(
        fingerprint_e != fingerprint_a,
        "Manifest fingerprint should change when mod content definitions change.");

    reordered[0].npcs[0].behavior = manifests[1].npcs[0].behavior;
    reordered[0].script_api_version = "9.9.9";
    const std::string fingerprint_f = novaria::mod::ModLoader::BuildManifestFingerprint(reordered);
    passed &= Expect(
        fingerprint_f != fingerprint_a,
        "Manifest fingerprint should change when script metadata changes.");

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

bool TestRejectInvalidContentDefinition() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);

    std::filesystem::create_directories(test_root / "mod_bad_content" / "content", ec);
    WriteTextFile(
        test_root / "mod_bad_content" / "mod.toml",
        "name = \"mod_bad_content\"\n"
        "version = \"1.0.0\"\n");
    WriteTextFile(
        test_root / "mod_bad_content" / "content" / "recipes.csv",
        "broken_recipe,wood,NaN,sword,1\n");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(test_root, error), "Initialize should succeed for test root.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(
        !loader.LoadAll(manifests, error),
        "LoadAll should fail when content definitions are invalid.");
    passed &= Expect(
        error.find("Invalid mod recipes file") != std::string::npos,
        "Invalid content failure should include source file category.");

    loader.Shutdown();
    std::filesystem::remove_all(test_root, ec);
    return passed;
}

bool TestRejectMissingDependency() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);

    std::filesystem::create_directories(test_root / "mod_missing_dep", ec);
    WriteTextFile(
        test_root / "mod_missing_dep" / "mod.toml",
        "name = \"mod_missing_dep\"\n"
        "version = \"1.0.0\"\n"
        "dependencies = [\"not_exists\"]\n");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(test_root, error), "Initialize should succeed for test root.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(
        !loader.LoadAll(manifests, error),
        "LoadAll should fail when dependency target is missing.");
    passed &= Expect(
        error.find("Missing dependency") != std::string::npos,
        "Missing dependency failure should include clear reason.");

    loader.Shutdown();
    std::filesystem::remove_all(test_root, ec);
    return passed;
}

bool TestRejectCyclicDependency() {
    bool passed = true;
    const std::filesystem::path test_root = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);

    std::filesystem::create_directories(test_root / "mod_cycle_a", ec);
    std::filesystem::create_directories(test_root / "mod_cycle_b", ec);
    WriteTextFile(
        test_root / "mod_cycle_a" / "mod.toml",
        "name = \"mod_cycle_a\"\n"
        "version = \"1.0.0\"\n"
        "dependencies = [\"mod_cycle_b\"]\n");
    WriteTextFile(
        test_root / "mod_cycle_b" / "mod.toml",
        "name = \"mod_cycle_b\"\n"
        "version = \"1.0.0\"\n"
        "dependencies = [\"mod_cycle_a\"]\n");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(test_root, error), "Initialize should succeed for test root.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(
        !loader.LoadAll(manifests, error),
        "LoadAll should fail on cyclic dependency graph.");
    passed &= Expect(
        error.find("Cyclic mod dependency") != std::string::npos,
        "Cyclic dependency failure should include clear reason.");

    loader.Shutdown();
    std::filesystem::remove_all(test_root, ec);
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestLoadAllAndFingerprint();
    passed &= TestRejectInvalidManifest();
    passed &= TestRejectInvalidContentDefinition();
    passed &= TestRejectMissingDependency();
    passed &= TestRejectCyclicDependency();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_mod_loader_tests\n";
    return 0;
}
