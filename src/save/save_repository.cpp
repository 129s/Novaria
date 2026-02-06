#include "save/save_repository.h"

#include "core/logger.h"

#include <fstream>
#include <limits>
#include <string>

namespace novaria::save {
namespace {

bool ParseUnsignedInteger(const std::string& text, std::uint64_t& out_value) {
    try {
        size_t consumed = 0;
        const std::uint64_t parsed = std::stoull(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        out_value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

bool FileSaveRepository::Initialize(const std::filesystem::path& save_root, std::string& out_error) {
    std::error_code ec;
    std::filesystem::create_directories(save_root, ec);
    if (ec) {
        out_error = "Failed to create save directory: " + ec.message();
        return false;
    }

    save_root_ = save_root;
    world_save_path_ = save_root_ / "world.sav";
    initialized_ = true;
    out_error.clear();

    core::Logger::Info("save", "FileSaveRepository initialized at: " + save_root_.string());
    return true;
}

void FileSaveRepository::Shutdown() {
    if (!initialized_) {
        return;
    }

    initialized_ = false;
    save_root_.clear();
    world_save_path_.clear();
}

bool FileSaveRepository::SaveWorldState(const WorldSaveState& state, std::string& out_error) {
    if (!initialized_) {
        out_error = "Save repository is not initialized.";
        return false;
    }

    std::ofstream file(world_save_path_, std::ios::trunc);
    if (!file.is_open()) {
        out_error = "Failed to open world save file for writing: " + world_save_path_.string();
        return false;
    }

    file << "tick_index=" << state.tick_index << '\n';
    file << "local_player_id=" << state.local_player_id << '\n';
    file << "mod_manifest_fingerprint=" << state.mod_manifest_fingerprint << '\n';
    file.close();

    out_error.clear();
    return true;
}

bool FileSaveRepository::LoadWorldState(WorldSaveState& out_state, std::string& out_error) {
    if (!initialized_) {
        out_error = "Save repository is not initialized.";
        return false;
    }

    if (!std::filesystem::exists(world_save_path_)) {
        out_error = "World save file does not exist.";
        return false;
    }

    std::ifstream file(world_save_path_);
    if (!file.is_open()) {
        out_error = "Failed to open world save file for reading: " + world_save_path_.string();
        return false;
    }

    WorldSaveState parsed_state{};
    std::string line;
    while (std::getline(file, line)) {
        const std::string::size_type equal_pos = line.find('=');
        if (equal_pos == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, equal_pos);
        const std::string value = line.substr(equal_pos + 1);

        if (key == "tick_index") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid tick_index value in world save file.";
                return false;
            }
            parsed_state.tick_index = parsed;
            continue;
        }

        if (key == "local_player_id") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed) ||
                parsed > std::numeric_limits<std::uint32_t>::max()) {
                out_error = "Invalid local_player_id value in world save file.";
                return false;
            }
            parsed_state.local_player_id = static_cast<std::uint32_t>(parsed);
            continue;
        }

        if (key == "mod_manifest_fingerprint") {
            parsed_state.mod_manifest_fingerprint = value;
            continue;
        }
    }

    out_state = parsed_state;
    out_error.clear();
    return true;
}

}  // namespace novaria::save
