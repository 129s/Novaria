#include "save/save_repository.h"

#include "core/logger.h"

#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace novaria::save {
namespace {

constexpr std::string_view kDebugNetSectionVersionKey = "debug_section.net.version";
constexpr std::string_view kDebugNetSessionTransitionsKey =
    "debug_section.net.session_transitions";
constexpr std::string_view kDebugNetTimeoutDisconnectsKey =
    "debug_section.net.timeout_disconnects";
constexpr std::string_view kDebugNetManualDisconnectsKey =
    "debug_section.net.manual_disconnects";
constexpr std::string_view kDebugNetLastHeartbeatTickKey =
    "debug_section.net.last_heartbeat_tick";
constexpr std::string_view kDebugNetDroppedCommandsKey =
    "debug_section.net.dropped_commands";
constexpr std::string_view kDebugNetDroppedRemotePayloadsKey =
    "debug_section.net.dropped_remote_payloads";
constexpr std::string_view kDebugNetLastTransitionReasonKey =
    "debug_section.net.last_transition_reason";

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

bool ParseUInt32Value(const std::string& text, std::uint32_t& out_value) {
    std::uint64_t parsed = 0;
    if (!ParseUnsignedInteger(text, parsed) ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    out_value = static_cast<std::uint32_t>(parsed);
    return true;
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
    file << "format_version=" << state.format_version << '\n';
    file << "local_player_id=" << state.local_player_id << '\n';
    file << "mod_manifest_fingerprint=" << state.mod_manifest_fingerprint << '\n';
    file << kDebugNetSectionVersionKey << "=" << kCurrentNetDebugSectionVersion << '\n';
    file << kDebugNetSessionTransitionsKey << "=" << state.debug_net_session_transitions << '\n';
    file << kDebugNetTimeoutDisconnectsKey << "=" << state.debug_net_timeout_disconnects << '\n';
    file << kDebugNetManualDisconnectsKey << "=" << state.debug_net_manual_disconnects << '\n';
    file << kDebugNetLastHeartbeatTickKey << "=" << state.debug_net_last_heartbeat_tick << '\n';
    file << kDebugNetDroppedCommandsKey << "=" << state.debug_net_dropped_commands << '\n';
    file << kDebugNetDroppedRemotePayloadsKey << "="
         << state.debug_net_dropped_remote_payloads << '\n';
    file << kDebugNetLastTransitionReasonKey << "=" << state.debug_net_last_transition_reason
         << '\n';
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
    parsed_state.format_version = 0;
    std::uint32_t parsed_debug_net_section_version = 0;
    bool has_debug_net_section_version = false;
    bool has_debug_net_section_fields = false;
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

        if (key == "format_version") {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed)) {
                out_error = "Invalid format_version value in world save file.";
                return false;
            }
            parsed_state.format_version = parsed;
            continue;
        }

        if (key == "local_player_id") {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed)) {
                out_error = "Invalid local_player_id value in world save file.";
                return false;
            }
            parsed_state.local_player_id = parsed;
            continue;
        }

        if (key == "mod_manifest_fingerprint") {
            parsed_state.mod_manifest_fingerprint = value;
            continue;
        }

        if (key == kDebugNetSectionVersionKey) {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed) || parsed == 0) {
                out_error = "Invalid debug_section.net.version value in world save file.";
                return false;
            }

            if (parsed > kCurrentNetDebugSectionVersion) {
                out_error = "Unsupported debug section net version: " + std::to_string(parsed);
                return false;
            }

            has_debug_net_section_version = true;
            parsed_debug_net_section_version = parsed;
            continue;
        }

        if (key == kDebugNetSessionTransitionsKey) {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_section.net.session_transitions value in world save file.";
                return false;
            }
            parsed_state.debug_net_session_transitions = parsed;
            has_debug_net_section_fields = true;
            continue;
        }

        if (key == kDebugNetTimeoutDisconnectsKey) {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_section.net.timeout_disconnects value in world save file.";
                return false;
            }
            parsed_state.debug_net_timeout_disconnects = parsed;
            has_debug_net_section_fields = true;
            continue;
        }

        if (key == kDebugNetManualDisconnectsKey) {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_section.net.manual_disconnects value in world save file.";
                return false;
            }
            parsed_state.debug_net_manual_disconnects = parsed;
            has_debug_net_section_fields = true;
            continue;
        }

        if (key == kDebugNetLastHeartbeatTickKey) {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_section.net.last_heartbeat_tick value in world save file.";
                return false;
            }
            parsed_state.debug_net_last_heartbeat_tick = parsed;
            has_debug_net_section_fields = true;
            continue;
        }

        if (key == kDebugNetDroppedCommandsKey) {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_section.net.dropped_commands value in world save file.";
                return false;
            }
            parsed_state.debug_net_dropped_commands = parsed;
            has_debug_net_section_fields = true;
            continue;
        }

        if (key == kDebugNetDroppedRemotePayloadsKey) {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_section.net.dropped_remote_payloads value in world save file.";
                return false;
            }
            parsed_state.debug_net_dropped_remote_payloads = parsed;
            has_debug_net_section_fields = true;
            continue;
        }

        if (key == kDebugNetLastTransitionReasonKey) {
            parsed_state.debug_net_last_transition_reason = value;
            has_debug_net_section_fields = true;
            continue;
        }

        if (key == "debug_net_session_transitions") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_net_session_transitions value in world save file.";
                return false;
            }
            parsed_state.debug_net_session_transitions = parsed;
            continue;
        }

        if (key == "debug_net_timeout_disconnects") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_net_timeout_disconnects value in world save file.";
                return false;
            }
            parsed_state.debug_net_timeout_disconnects = parsed;
            continue;
        }

        if (key == "debug_net_manual_disconnects") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_net_manual_disconnects value in world save file.";
                return false;
            }
            parsed_state.debug_net_manual_disconnects = parsed;
            continue;
        }

        if (key == "debug_net_last_heartbeat_tick") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_net_last_heartbeat_tick value in world save file.";
                return false;
            }
            parsed_state.debug_net_last_heartbeat_tick = parsed;
            continue;
        }

        if (key == "debug_net_dropped_commands") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_net_dropped_commands value in world save file.";
                return false;
            }
            parsed_state.debug_net_dropped_commands = parsed;
            continue;
        }

        if (key == "debug_net_dropped_remote_payloads") {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid debug_net_dropped_remote_payloads value in world save file.";
                return false;
            }
            parsed_state.debug_net_dropped_remote_payloads = parsed;
            continue;
        }

        if (key == "debug_net_last_transition_reason") {
            parsed_state.debug_net_last_transition_reason = value;
            continue;
        }
    }

    if (has_debug_net_section_fields && !has_debug_net_section_version) {
        out_error = "Missing debug_section.net.version for debug section fields.";
        return false;
    }

    if (has_debug_net_section_version && parsed_debug_net_section_version == 0) {
        out_error = "Invalid debug_section.net.version value in world save file.";
        return false;
    }

    if (parsed_state.format_version > kCurrentWorldSaveFormatVersion) {
        out_error =
            "Unsupported world save format version: " + std::to_string(parsed_state.format_version);
        return false;
    }

    out_state = parsed_state;
    out_error.clear();
    return true;
}

}  // namespace novaria::save
