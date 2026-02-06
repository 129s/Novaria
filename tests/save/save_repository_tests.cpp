#include "save/save_repository.h"

#include <filesystem>
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
        .tick_index = 12345,
        .local_player_id = 9,
    };
    passed &= Expect(repository.SaveWorldState(expected, error), "Save should succeed.");
    passed &= Expect(error.empty(), "Save should not return error.");

    novaria::save::WorldSaveState actual{};
    passed &= Expect(repository.LoadWorldState(actual, error), "Load should succeed after save.");
    passed &= Expect(error.empty(), "Load should not return error.");
    passed &= Expect(actual.tick_index == expected.tick_index, "Loaded tick should match saved value.");
    passed &= Expect(
        actual.local_player_id == expected.local_player_id,
        "Loaded local player id should match saved value.");

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
