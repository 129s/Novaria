#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace novaria::save {

struct WorldSaveState final {
    std::uint64_t tick_index = 0;
    std::uint32_t local_player_id = 0;
    std::string mod_manifest_fingerprint;
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
};

}  // namespace novaria::save
