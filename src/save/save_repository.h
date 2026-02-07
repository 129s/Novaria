#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace novaria::save {

inline constexpr std::uint32_t kCurrentWorldSaveFormatVersion = 1;
inline constexpr std::uint32_t kCurrentWorldSectionVersion = 1;
inline constexpr std::uint32_t kCurrentNetDebugSectionVersion = 1;

struct WorldSaveState final {
    std::uint32_t format_version = kCurrentWorldSaveFormatVersion;
    std::uint64_t tick_index = 0;
    std::uint32_t local_player_id = 0;
    std::string mod_manifest_fingerprint;
    std::uint32_t gameplay_wood_collected = 0;
    std::uint32_t gameplay_stone_collected = 0;
    bool gameplay_workbench_built = false;
    bool gameplay_sword_crafted = false;
    std::uint32_t gameplay_enemy_kill_count = 0;
    std::uint32_t gameplay_boss_health = 0;
    bool gameplay_boss_defeated = false;
    bool gameplay_loop_complete = false;
    bool has_gameplay_snapshot = false;
    std::vector<std::string> world_chunk_payloads;
    bool has_world_snapshot = false;
    std::uint64_t debug_net_session_transitions = 0;
    std::uint64_t debug_net_timeout_disconnects = 0;
    std::uint64_t debug_net_manual_disconnects = 0;
    std::uint64_t debug_net_last_heartbeat_tick = 0;
    std::uint64_t debug_net_dropped_commands = 0;
    std::uint64_t debug_net_dropped_remote_payloads = 0;
    std::string debug_net_last_transition_reason;
};

class ISaveRepository {
public:
    virtual ~ISaveRepository() = default;

    virtual bool Initialize(const std::filesystem::path& save_root, std::string& out_error) = 0;
    virtual void Shutdown() = 0;
    virtual bool SaveWorldState(const WorldSaveState& state, std::string& out_error) = 0;
    virtual bool LoadWorldState(WorldSaveState& out_state, std::string& out_error) = 0;
};

class FileSaveRepository final : public ISaveRepository {
public:
    bool Initialize(const std::filesystem::path& save_root, std::string& out_error) override;
    void Shutdown() override;
    bool SaveWorldState(const WorldSaveState& state, std::string& out_error) override;
    bool LoadWorldState(WorldSaveState& out_state, std::string& out_error) override;

private:
    bool initialized_ = false;
    std::filesystem::path save_root_;
    std::filesystem::path world_save_path_;
    std::filesystem::path world_save_backup_path_;
};

}  // namespace novaria::save
