#include "save/save_repository.h"

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
    return std::filesystem::temp_directory_path() / "novaria_save_repo_test";
}

}  // namespace

int main() {
    bool passed = true;
    const std::filesystem::path test_dir = BuildTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    novaria::save::FileSaveRepository repository;
    std::string error;

    passed &= Expect(repository.Initialize(test_dir, error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error.");

    novaria::save::WorldSaveState loaded{};
    passed &= Expect(!repository.LoadWorldState(loaded, error), "Load should fail when save file does not exist.");
    passed &= Expect(!error.empty(), "Load without file should return a reason.");

    const novaria::save::WorldSaveState expected{
        .format_version = novaria::save::kCurrentWorldSaveFormatVersion,
        .tick_index = 12345,
        .local_player_id = 9,
        .mod_manifest_fingerprint = "mods:v1:abc123",
        .debug_net_session_transitions = 7,
        .debug_net_timeout_disconnects = 2,
        .debug_net_manual_disconnects = 3,
        .debug_net_dropped_commands = 11,
        .debug_net_dropped_remote_payloads = 5,
    };
    passed &= Expect(repository.SaveWorldState(expected, error), "Save should succeed.");
    passed &= Expect(error.empty(), "Save should not return error.");

    novaria::save::WorldSaveState actual{};
    passed &= Expect(repository.LoadWorldState(actual, error), "Load should succeed after save.");
    passed &= Expect(error.empty(), "Load should not return error.");
    passed &= Expect(actual.tick_index == expected.tick_index, "Loaded tick should match saved value.");
    passed &= Expect(
        actual.format_version == expected.format_version,
        "Loaded format version should match saved value.");
    passed &= Expect(
        actual.local_player_id == expected.local_player_id,
        "Loaded local player id should match saved value.");
    passed &= Expect(
        actual.mod_manifest_fingerprint == expected.mod_manifest_fingerprint,
        "Loaded mod manifest fingerprint should match saved value.");
    passed &= Expect(
        actual.debug_net_session_transitions == expected.debug_net_session_transitions,
        "Loaded debug net session transitions should match saved value.");
    passed &= Expect(
        actual.debug_net_timeout_disconnects == expected.debug_net_timeout_disconnects,
        "Loaded debug net timeout disconnects should match saved value.");
    passed &= Expect(
        actual.debug_net_manual_disconnects == expected.debug_net_manual_disconnects,
        "Loaded debug net manual disconnects should match saved value.");
    passed &= Expect(
        actual.debug_net_dropped_commands == expected.debug_net_dropped_commands,
        "Loaded debug net dropped commands should match saved value.");
    passed &= Expect(
        actual.debug_net_dropped_remote_payloads == expected.debug_net_dropped_remote_payloads,
        "Loaded debug net dropped remote payloads should match saved value.");

    std::ofstream legacy_file(test_dir / "world.sav", std::ios::trunc);
    legacy_file << "tick_index=77\n";
    legacy_file << "local_player_id=5\n";
    legacy_file.close();

    novaria::save::WorldSaveState legacy_loaded{};
    passed &= Expect(repository.LoadWorldState(legacy_loaded, error), "Legacy save format should still load.");
    passed &= Expect(error.empty(), "Legacy save load should not return error.");
    passed &= Expect(legacy_loaded.format_version == 0, "Legacy save format version should default to 0.");
    passed &= Expect(legacy_loaded.tick_index == 77, "Legacy loaded tick should match.");
    passed &= Expect(legacy_loaded.local_player_id == 5, "Legacy loaded player id should match.");
    passed &= Expect(
        legacy_loaded.mod_manifest_fingerprint.empty(),
        "Legacy save without fingerprint should default to empty fingerprint.");
    passed &= Expect(
        legacy_loaded.debug_net_session_transitions == 0 &&
            legacy_loaded.debug_net_timeout_disconnects == 0 &&
            legacy_loaded.debug_net_manual_disconnects == 0 &&
            legacy_loaded.debug_net_dropped_commands == 0 &&
            legacy_loaded.debug_net_dropped_remote_payloads == 0,
        "Legacy save without debug net snapshot should default debug counters to zero.");

    std::ofstream future_file(test_dir / "world.sav", std::ios::trunc);
    future_file << "format_version=" << (novaria::save::kCurrentWorldSaveFormatVersion + 1) << "\n";
    future_file << "tick_index=1\n";
    future_file << "local_player_id=1\n";
    future_file.close();

    novaria::save::WorldSaveState future_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(future_loaded, error),
        "Future save format version should be rejected.");
    passed &= Expect(!error.empty(), "Future save rejection should include reason.");

    repository.Shutdown();
    passed &= Expect(
        !repository.SaveWorldState(expected, error),
        "Save should fail after shutdown.");
    passed &= Expect(!error.empty(), "Save failure after shutdown should provide a reason.");

    std::filesystem::remove_all(test_dir, ec);

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_save_repository_tests\n";
    return 0;
}
