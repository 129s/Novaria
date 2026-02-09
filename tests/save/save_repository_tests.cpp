#include "save/save_repository.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <Windows.h>
#endif

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
        ("novaria_save_repo_test_" + std::to_string(unique_seed));
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
        .gameplay_wood_collected = 42,
        .gameplay_stone_collected = 27,
        .gameplay_workbench_built = true,
        .gameplay_sword_crafted = true,
        .gameplay_enemy_kill_count = 3,
        .gameplay_boss_health = 0,
        .gameplay_boss_defeated = true,
        .gameplay_loop_complete = true,
        .has_gameplay_snapshot = true,
        .world_chunk_payloads = {
            novaria::wire::ByteBuffer{0x00, 0x01, 0x02, 0x03, 0x04},
            novaria::wire::ByteBuffer{0xFE, 0xFD, 0x00, 0x80},
        },
        .has_world_snapshot = true,
        .debug_net_session_transitions = 7,
        .debug_net_timeout_disconnects = 2,
        .debug_net_manual_disconnects = 3,
        .debug_net_last_heartbeat_tick = 4096,
        .debug_net_dropped_commands = 11,
        .debug_net_dropped_remote_payloads = 5,
        .debug_net_last_transition_reason = "heartbeat_timeout",
    };
    passed &= Expect(repository.SaveWorldState(expected, error), "Save should succeed.");
    passed &= Expect(error.empty(), "Save should not return error.");

    std::ifstream saved_file(test_dir / "world.sav");
    bool found_gameplay_section_version = false;
    bool found_gameplay_section_field = false;
    bool found_world_section_version = false;
    bool found_world_section_count = false;
    bool found_world_section_chunk_entry = false;
    bool found_debug_section_version = false;
    bool found_debug_section_counter = false;
    bool found_legacy_debug_counter = false;
    std::string saved_line;
    while (std::getline(saved_file, saved_line)) {
        if (saved_line == "gameplay_section.core.version=1") {
            found_gameplay_section_version = true;
        }

        if (saved_line.rfind("gameplay_section.core.enemy_kill_count=", 0) == 0) {
            found_gameplay_section_field = true;
        }

        if (saved_line == "world_section.core.version=1") {
            found_world_section_version = true;
        }

        if (saved_line == "world_section.core.chunk_count=2") {
            found_world_section_count = true;
        }

        if (saved_line.rfind("world_section.core.chunk.0=", 0) == 0) {
            found_world_section_chunk_entry = true;
        }

        if (saved_line == "debug_section.net.version=" +
                std::to_string(novaria::save::kCurrentNetDebugSectionVersion)) {
            found_debug_section_version = true;
        }

        if (saved_line.rfind("debug_section.net.session_transitions=", 0) == 0) {
            found_debug_section_counter = true;
        }

        if (saved_line.rfind("debug_net_session_transitions=", 0) == 0) {
            found_legacy_debug_counter = true;
        }
    }
    saved_file.close();
    passed &= Expect(
        found_gameplay_section_version,
        "Saved file should include gameplay_section.core.version.");
    passed &= Expect(
        found_gameplay_section_field,
        "Saved file should include gameplay section fields.");
    passed &= Expect(
        found_world_section_version,
        "Saved file should include world_section.core.version.");
    passed &= Expect(
        found_world_section_count,
        "Saved file should include world_section.core.chunk_count.");
    passed &= Expect(
        found_world_section_chunk_entry,
        "Saved file should include world section chunk entries.");
    passed &= Expect(
        found_debug_section_version,
        "Saved file should include debug_section.net.version.");
    passed &= Expect(
        found_debug_section_counter,
        "Saved file should include versioned debug section counters.");
    passed &= Expect(
        !found_legacy_debug_counter,
        "Saved file should not emit legacy flat debug_net_* counters.");

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
        actual.gameplay_wood_collected == expected.gameplay_wood_collected &&
            actual.gameplay_stone_collected == expected.gameplay_stone_collected,
        "Loaded gameplay resource counters should match saved values.");
    passed &= Expect(
        actual.gameplay_workbench_built == expected.gameplay_workbench_built &&
            actual.gameplay_sword_crafted == expected.gameplay_sword_crafted,
        "Loaded gameplay craft flags should match saved values.");
    passed &= Expect(
        actual.gameplay_enemy_kill_count == expected.gameplay_enemy_kill_count &&
            actual.gameplay_boss_health == expected.gameplay_boss_health &&
            actual.gameplay_boss_defeated == expected.gameplay_boss_defeated &&
            actual.gameplay_loop_complete == expected.gameplay_loop_complete,
        "Loaded gameplay combat progress should match saved values.");
    passed &= Expect(
        actual.has_gameplay_snapshot,
        "Loaded state should mark gameplay snapshot as present.");
    passed &= Expect(
        actual.has_world_snapshot,
        "Loaded state should mark world snapshot as present.");
    passed &= Expect(
        actual.world_chunk_payloads == expected.world_chunk_payloads,
        "Loaded world chunk payloads should match saved values.");
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
        actual.debug_net_last_heartbeat_tick == expected.debug_net_last_heartbeat_tick,
        "Loaded debug net last heartbeat tick should match saved value.");
    passed &= Expect(
        actual.debug_net_dropped_commands == expected.debug_net_dropped_commands,
        "Loaded debug net dropped commands should match saved value.");
    passed &= Expect(
        actual.debug_net_dropped_remote_payloads == expected.debug_net_dropped_remote_payloads,
        "Loaded debug net dropped remote payloads should match saved value.");
    passed &= Expect(
        actual.debug_net_last_transition_reason == expected.debug_net_last_transition_reason,
        "Loaded debug net last transition reason should match saved value.");

    novaria::save::WorldSaveState updated_save = expected;
    updated_save.tick_index = expected.tick_index + 1;
    passed &= Expect(
        repository.SaveWorldState(updated_save, error),
        "Second save should succeed and generate backup.");
    passed &= Expect(error.empty(), "Second save should not return error.");

    const std::filesystem::path backup_path = test_dir / "world.sav.bak";
    passed &= Expect(
        std::filesystem::exists(backup_path),
        "Second save should generate world.sav.bak.");
    std::ifstream backup_file(backup_path);
    bool backup_kept_old_tick = false;
    while (std::getline(backup_file, saved_line)) {
        if (saved_line == "tick_index=12345") {
            backup_kept_old_tick = true;
            break;
        }
    }
    passed &= Expect(
        backup_kept_old_tick,
        "Backup save should keep previous world.sav content.");

    std::ofstream legacy_file(test_dir / "world.sav", std::ios::trunc);
    legacy_file << "tick_index=77\n";
    legacy_file << "local_player_id=5\n";
    legacy_file.close();

    novaria::save::WorldSaveState legacy_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(legacy_loaded, error),
        "Legacy save format should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Legacy save rejection should provide a reason.");

    std::ofstream legacy_debug_file(test_dir / "world.sav", std::ios::trunc);
    legacy_debug_file << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion << "\n";
    legacy_debug_file << "tick_index=88\n";
    legacy_debug_file << "local_player_id=4\n";
    legacy_debug_file << "debug_net_session_transitions=19\n";
    legacy_debug_file << "debug_net_timeout_disconnects=6\n";
    legacy_debug_file << "debug_net_manual_disconnects=7\n";
    legacy_debug_file << "debug_net_last_heartbeat_tick=2048\n";
    legacy_debug_file << "debug_net_dropped_commands=3\n";
    legacy_debug_file << "debug_net_dropped_remote_payloads=9\n";
    legacy_debug_file << "debug_net_last_transition_reason=request_disconnect\n";
    legacy_debug_file.close();

    novaria::save::WorldSaveState legacy_debug_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(legacy_debug_loaded, error),
        "Legacy flat debug_net_* fields should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Legacy debug rejection should provide a reason.");

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

    std::ofstream future_gameplay_section_file(test_dir / "world.sav", std::ios::trunc);
    future_gameplay_section_file
        << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion << "\n";
    future_gameplay_section_file << "tick_index=1\n";
    future_gameplay_section_file << "local_player_id=1\n";
    future_gameplay_section_file << "gameplay_section.core.version=2\n";
    future_gameplay_section_file << "gameplay_section.core.loop_complete=true\n";
    future_gameplay_section_file.close();

    novaria::save::WorldSaveState future_gameplay_section_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(future_gameplay_section_loaded, error),
        "Future gameplay section version should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Future gameplay section rejection should include reason.");

    std::ofstream future_debug_section_file(test_dir / "world.sav", std::ios::trunc);
    future_debug_section_file << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion
                              << "\n";
    future_debug_section_file << "tick_index=1\n";
    future_debug_section_file << "local_player_id=1\n";
    future_debug_section_file
        << "debug_section.net.version=" << (novaria::save::kCurrentNetDebugSectionVersion + 1)
        << "\n";
    future_debug_section_file << "debug_section.net.dropped_commands=1\n";
    future_debug_section_file.close();

    novaria::save::WorldSaveState future_debug_section_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(future_debug_section_loaded, error),
        "Future debug section version should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Future debug section rejection should include reason.");

    std::ofstream invalid_gameplay_file(test_dir / "world.sav", std::ios::trunc);
    invalid_gameplay_file
        << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion << "\n";
    invalid_gameplay_file << "tick_index=1\n";
    invalid_gameplay_file << "local_player_id=1\n";
    invalid_gameplay_file << "gameplay_section.core.version=1\n";
    invalid_gameplay_file << "gameplay_section.core.loop_complete=maybe\n";
    invalid_gameplay_file.close();

    novaria::save::WorldSaveState invalid_gameplay_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(invalid_gameplay_loaded, error),
        "Invalid gameplay section value should fail save load.");
    passed &= Expect(
        !error.empty(),
        "Invalid gameplay section load failure should include reason.");

    std::ofstream missing_gameplay_version_file(test_dir / "world.sav", std::ios::trunc);
    missing_gameplay_version_file
        << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion << "\n";
    missing_gameplay_version_file << "tick_index=1\n";
    missing_gameplay_version_file << "local_player_id=1\n";
    missing_gameplay_version_file << "gameplay_section.core.loop_complete=true\n";
    missing_gameplay_version_file.close();

    novaria::save::WorldSaveState missing_gameplay_version_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(missing_gameplay_version_loaded, error),
        "Gameplay section fields without version should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Missing gameplay section version should include reason.");

    std::ofstream missing_world_version_file(test_dir / "world.sav", std::ios::trunc);
    missing_world_version_file
        << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion << "\n";
    missing_world_version_file << "tick_index=1\n";
    missing_world_version_file << "local_player_id=1\n";
    missing_world_version_file << "world_section.core.chunk_count=1\n";
    missing_world_version_file << "world_section.core.chunk.0=0,0,4,1,2,3,4\n";
    missing_world_version_file.close();

    novaria::save::WorldSaveState missing_world_version_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(missing_world_version_loaded, error),
        "World section fields without version should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Missing world section version should include reason.");

    std::ofstream mismatched_world_chunk_count_file(test_dir / "world.sav", std::ios::trunc);
    mismatched_world_chunk_count_file
        << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion << "\n";
    mismatched_world_chunk_count_file << "tick_index=1\n";
    mismatched_world_chunk_count_file << "local_player_id=1\n";
    mismatched_world_chunk_count_file << "world_section.core.version=1\n";
    mismatched_world_chunk_count_file << "world_section.core.chunk_count=2\n";
    mismatched_world_chunk_count_file << "world_section.core.chunk.0=0,0,4,1,2,3,4\n";
    mismatched_world_chunk_count_file.close();

    novaria::save::WorldSaveState mismatched_world_chunk_count_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(mismatched_world_chunk_count_loaded, error),
        "Mismatched world chunk count should fail save load.");
    passed &= Expect(
        !error.empty(),
        "Mismatched world chunk count should include reason.");

    std::ofstream invalid_debug_file(test_dir / "world.sav", std::ios::trunc);
    invalid_debug_file << "format_version=" << novaria::save::kCurrentWorldSaveFormatVersion << "\n";
    invalid_debug_file << "tick_index=1\n";
    invalid_debug_file << "local_player_id=1\n";
    invalid_debug_file << "debug_section.net.version=" << novaria::save::kCurrentNetDebugSectionVersion
                       << "\n";
    invalid_debug_file << "debug_section.net.dropped_commands=NaN\n";
    invalid_debug_file.close();

    novaria::save::WorldSaveState invalid_debug_loaded{};
    passed &= Expect(
        !repository.LoadWorldState(invalid_debug_loaded, error),
        "Invalid debug section value should fail save load.");
    passed &= Expect(
        !error.empty(),
        "Invalid debug section load failure should include reason.");

#if defined(_WIN32)
    novaria::save::WorldSaveState locked_save_state = expected;
    locked_save_state.tick_index = expected.tick_index + 100;
    const std::filesystem::path world_save_path = test_dir / "world.sav";
    const std::filesystem::path world_tmp_path = test_dir / "world.sav.tmp";
    HANDLE locked_file_handle = CreateFileW(
        world_save_path.wstring().c_str(),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    passed &= Expect(
        locked_file_handle != INVALID_HANDLE_VALUE,
        "World save lock handle should open for replace-failure test.");
    if (locked_file_handle != INVALID_HANDLE_VALUE) {
        std::string locked_save_error;
        passed &= Expect(
            !repository.SaveWorldState(locked_save_state, locked_save_error),
            "Save should fail when world.sav is exclusively locked.");
        passed &= Expect(
            !locked_save_error.empty(),
            "Replace failure under lock should provide readable error.");
        passed &= Expect(
            std::filesystem::exists(world_tmp_path),
            "Replace failure should keep world.sav.tmp for recovery.");
        CloseHandle(locked_file_handle);

        passed &= Expect(
            repository.SaveWorldState(locked_save_state, error),
            "Save should recover once file lock is released.");
        passed &= Expect(
            error.empty(),
            "Recovered save should not return error.");
    }
#endif

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
