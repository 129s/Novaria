#include "save/save_repository.h"

#include "core/base64.h"
#include "core/logger.h"

#include <fstream>
#include <limits>
#include <system_error>
#include <string>
#include <string_view>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace novaria::save {
namespace {

constexpr std::string_view kWorldSectionVersionKey = "world_section.core.version";
constexpr std::string_view kWorldChunkCountKey = "world_section.core.chunk_count";
constexpr std::string_view kWorldChunkEntryPrefix = "world_section.core.chunk.";
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
constexpr std::string_view kGameplaySectionVersionKey = "gameplay_section.core.version";
constexpr std::string_view kGameplayWoodCollectedKey = "gameplay_section.core.wood_collected";
constexpr std::string_view kGameplayStoneCollectedKey = "gameplay_section.core.stone_collected";
constexpr std::string_view kGameplayWorkbenchBuiltKey = "gameplay_section.core.workbench_built";
constexpr std::string_view kGameplaySwordCraftedKey = "gameplay_section.core.sword_crafted";
constexpr std::string_view kGameplayEnemyKillCountKey = "gameplay_section.core.enemy_kill_count";
constexpr std::string_view kGameplayBossHealthKey = "gameplay_section.core.boss_health";
constexpr std::string_view kGameplayBossDefeatedKey = "gameplay_section.core.boss_defeated";
constexpr std::string_view kGameplayLoopCompleteKey = "gameplay_section.core.loop_complete";
constexpr std::uint32_t kCurrentGameplaySectionVersion = 1;

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

bool ParseBoolValue(const std::string& text, bool& out_value) {
    if (text == "true") {
        out_value = true;
        return true;
    }
    if (text == "false") {
        out_value = false;
        return true;
    }
    return false;
}

bool ReplaceSaveFileAtomically(
    const std::filesystem::path& source_path,
    const std::filesystem::path& target_path,
    std::string& out_error) {
#if defined(_WIN32)
    if (MoveFileExW(
            source_path.wstring().c_str(),
            target_path.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        const DWORD last_error = GetLastError();
        out_error =
            "Failed to replace world save file: " +
            std::system_category().message(static_cast<int>(last_error));
        return false;
    }

    out_error.clear();
    return true;
#else
    std::error_code ec;
    std::filesystem::rename(source_path, target_path, ec);
    if (ec) {
        out_error = "Failed to replace world save file: " + ec.message();
        return false;
    }

    out_error.clear();
    return true;
#endif
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
    world_save_backup_path_ = save_root_ / "world.sav.bak";
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
    world_save_backup_path_.clear();
}

bool FileSaveRepository::SaveWorldState(const WorldSaveState& state, std::string& out_error) {
    if (!initialized_) {
        out_error = "Save repository is not initialized.";
        return false;
    }
    if (state.format_version != kCurrentWorldSaveFormatVersion) {
        out_error =
            "Unsupported world save format version for write: " + std::to_string(state.format_version);
        return false;
    }

    const std::filesystem::path temp_save_path = world_save_path_.string() + ".tmp";
    std::ofstream file(temp_save_path, std::ios::trunc);
    if (!file.is_open()) {
        out_error = "Failed to open world save temp file for writing: " + temp_save_path.string();
        return false;
    }

    file << "tick_index=" << state.tick_index << '\n';
    file << "format_version=" << state.format_version << '\n';
    file << "local_player_id=" << state.local_player_id << '\n';
    file << "gameplay_fingerprint=" << state.gameplay_fingerprint << '\n';
    file << "cosmetic_fingerprint=" << state.cosmetic_fingerprint << '\n';
    if (state.has_gameplay_snapshot) {
        file << kGameplaySectionVersionKey << "=" << kCurrentGameplaySectionVersion << '\n';
        file << kGameplayWoodCollectedKey << "=" << state.gameplay_wood_collected << '\n';
        file << kGameplayStoneCollectedKey << "=" << state.gameplay_stone_collected << '\n';
        file << kGameplayWorkbenchBuiltKey << "="
             << (state.gameplay_workbench_built ? "true" : "false") << '\n';
        file << kGameplaySwordCraftedKey << "="
             << (state.gameplay_sword_crafted ? "true" : "false") << '\n';
        file << kGameplayEnemyKillCountKey << "=" << state.gameplay_enemy_kill_count << '\n';
        file << kGameplayBossHealthKey << "=" << state.gameplay_boss_health << '\n';
        file << kGameplayBossDefeatedKey << "="
             << (state.gameplay_boss_defeated ? "true" : "false") << '\n';
        file << kGameplayLoopCompleteKey << "="
             << (state.gameplay_loop_complete ? "true" : "false") << '\n';
    }
    if (state.has_world_snapshot) {
        file << kWorldSectionVersionKey << "=" << kCurrentWorldSectionVersion << '\n';
        file << kWorldChunkCountKey << "=" << state.world_chunk_payloads.size() << '\n';
        for (std::size_t index = 0; index < state.world_chunk_payloads.size(); ++index) {
            const auto& chunk_payload = state.world_chunk_payloads[index];
            const std::string encoded = core::Base64Encode(chunk_payload);
            file << kWorldChunkEntryPrefix << index << "="
                 << encoded << '\n';
        }
    }
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

    std::error_code ec;
    if (std::filesystem::exists(world_save_path_)) {
        std::filesystem::copy_file(
            world_save_path_,
            world_save_backup_path_,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            out_error = "Failed to write world save backup: " + ec.message();
            core::Logger::Warn(
                "save",
                "World save backup failed; keep temp file for recovery: " +
                    temp_save_path.string());
            return false;
        }
    }

    if (!ReplaceSaveFileAtomically(temp_save_path, world_save_path_, out_error)) {
        core::Logger::Warn(
            "save",
            "World save replace failed; keep temp file for recovery: " +
                temp_save_path.string());
        return false;
    }

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
    std::uint32_t parsed_gameplay_section_version = 0;
    bool has_gameplay_section_version = false;
    bool has_gameplay_section_fields = false;
    std::uint32_t parsed_world_section_version = 0;
    bool has_world_section_version = false;
    bool has_world_section_fields = false;
    bool has_world_chunk_count = false;
    std::size_t parsed_world_chunk_count = 0;
    std::unordered_map<std::size_t, wire::ByteBuffer> indexed_world_chunks;
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

        if (key == "gameplay_fingerprint") {
            parsed_state.gameplay_fingerprint = value;
            continue;
        }

        if (key == "cosmetic_fingerprint") {
            parsed_state.cosmetic_fingerprint = value;
            continue;
        }

        if (key == kGameplaySectionVersionKey) {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed) || parsed == 0) {
                out_error = "Invalid gameplay_section.core.version value in world save file.";
                return false;
            }

            if (parsed > kCurrentGameplaySectionVersion) {
                out_error = "Unsupported gameplay section core version: " + std::to_string(parsed);
                return false;
            }

            has_gameplay_section_version = true;
            parsed_gameplay_section_version = parsed;
            continue;
        }

        if (key == kGameplayWoodCollectedKey) {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed)) {
                out_error = "Invalid gameplay_section.core.wood_collected value in world save file.";
                return false;
            }
            parsed_state.gameplay_wood_collected = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kGameplayStoneCollectedKey) {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed)) {
                out_error = "Invalid gameplay_section.core.stone_collected value in world save file.";
                return false;
            }
            parsed_state.gameplay_stone_collected = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kGameplayWorkbenchBuiltKey) {
            bool parsed = false;
            if (!ParseBoolValue(value, parsed)) {
                out_error = "Invalid gameplay_section.core.workbench_built value in world save file.";
                return false;
            }
            parsed_state.gameplay_workbench_built = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kGameplaySwordCraftedKey) {
            bool parsed = false;
            if (!ParseBoolValue(value, parsed)) {
                out_error = "Invalid gameplay_section.core.sword_crafted value in world save file.";
                return false;
            }
            parsed_state.gameplay_sword_crafted = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kGameplayEnemyKillCountKey) {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed)) {
                out_error = "Invalid gameplay_section.core.enemy_kill_count value in world save file.";
                return false;
            }
            parsed_state.gameplay_enemy_kill_count = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kGameplayBossHealthKey) {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed)) {
                out_error = "Invalid gameplay_section.core.boss_health value in world save file.";
                return false;
            }
            parsed_state.gameplay_boss_health = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kGameplayBossDefeatedKey) {
            bool parsed = false;
            if (!ParseBoolValue(value, parsed)) {
                out_error = "Invalid gameplay_section.core.boss_defeated value in world save file.";
                return false;
            }
            parsed_state.gameplay_boss_defeated = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kGameplayLoopCompleteKey) {
            bool parsed = false;
            if (!ParseBoolValue(value, parsed)) {
                out_error = "Invalid gameplay_section.core.loop_complete value in world save file.";
                return false;
            }
            parsed_state.gameplay_loop_complete = parsed;
            has_gameplay_section_fields = true;
            continue;
        }

        if (key == kWorldSectionVersionKey) {
            std::uint32_t parsed = 0;
            if (!ParseUInt32Value(value, parsed) || parsed == 0) {
                out_error = "Invalid world_section.core.version value in world save file.";
                return false;
            }

            if (parsed > kCurrentWorldSectionVersion) {
                out_error = "Unsupported world section core version: " + std::to_string(parsed);
                return false;
            }

            has_world_section_version = true;
            parsed_world_section_version = parsed;
            continue;
        }

        if (key == kWorldChunkCountKey) {
            std::uint64_t parsed = 0;
            if (!ParseUnsignedInteger(value, parsed)) {
                out_error = "Invalid world_section.core.chunk_count value in world save file.";
                return false;
            }

            has_world_chunk_count = true;
            parsed_world_chunk_count = static_cast<std::size_t>(parsed);
            has_world_section_fields = true;
            continue;
        }

        if (key.rfind(kWorldChunkEntryPrefix, 0) == 0) {
            const std::string index_text =
                key.substr(kWorldChunkEntryPrefix.size());
            std::uint64_t parsed_index = 0;
            if (!ParseUnsignedInteger(index_text, parsed_index)) {
                out_error = "Invalid world_section.core.chunk.<index> key in world save file.";
                return false;
            }

            const std::size_t chunk_index = static_cast<std::size_t>(parsed_index);
            std::vector<std::uint8_t> decoded_bytes;
            if (!core::TryBase64Decode(value, decoded_bytes, out_error)) {
                out_error = "Invalid base64 world chunk payload: " + out_error;
                return false;
            }
            wire::ByteBuffer chunk_payload(decoded_bytes.begin(), decoded_bytes.end());
            const auto [_, inserted] = indexed_world_chunks.emplace(chunk_index, std::move(chunk_payload));
            if (!inserted) {
                out_error =
                    "Duplicated world chunk payload index in world save file: " +
                    std::to_string(chunk_index);
                return false;
            }

            has_world_section_fields = true;
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

        if (key.rfind("debug_net_", 0) == 0) {
            out_error =
                "Legacy debug_net_* fields are not supported; use debug_section.net.* instead.";
            return false;
        }
    }

    if (has_gameplay_section_fields && !has_gameplay_section_version) {
        out_error = "Missing gameplay_section.core.version for gameplay section fields.";
        return false;
    }

    if (has_gameplay_section_version && parsed_gameplay_section_version == 0) {
        out_error = "Invalid gameplay_section.core.version value in world save file.";
        return false;
    }

    if (has_world_section_fields && !has_world_section_version) {
        out_error = "Missing world_section.core.version for world section fields.";
        return false;
    }

    if (has_world_section_version && parsed_world_section_version == 0) {
        out_error = "Invalid world_section.core.version value in world save file.";
        return false;
    }

    if (!indexed_world_chunks.empty() && !has_world_chunk_count) {
        out_error = "Missing world_section.core.chunk_count for world chunk payload fields.";
        return false;
    }

    if (has_world_chunk_count && indexed_world_chunks.size() != parsed_world_chunk_count) {
        out_error =
            "world_section.core.chunk_count does not match payload entries.";
        return false;
    }

    if (has_world_chunk_count) {
        parsed_state.world_chunk_payloads.clear();
        parsed_state.world_chunk_payloads.reserve(parsed_world_chunk_count);
        for (std::size_t chunk_index = 0; chunk_index < parsed_world_chunk_count; ++chunk_index) {
            auto chunk_it = indexed_world_chunks.find(chunk_index);
            if (chunk_it == indexed_world_chunks.end()) {
                out_error =
                    "Missing world chunk payload index in world save file: " +
                    std::to_string(chunk_index);
                return false;
            }
            parsed_state.world_chunk_payloads.push_back(chunk_it->second);
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

    if (parsed_state.format_version != kCurrentWorldSaveFormatVersion) {
        out_error = "Unsupported world save format version: " +
            std::to_string(parsed_state.format_version) +
            " (expected " + std::to_string(kCurrentWorldSaveFormatVersion) + ")";
        return false;
    }

    parsed_state.has_gameplay_snapshot =
        has_gameplay_section_version || has_gameplay_section_fields;
    parsed_state.has_world_snapshot =
        has_world_section_version || has_world_section_fields;
    out_state = parsed_state;
    out_error.clear();
    return true;
}

}  // namespace novaria::save
