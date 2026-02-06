#pragma once

#include "core/config.h"
#include "mod/mod_loader.h"
#include "platform/sdl_context.h"
#include "save/save_repository.h"
#include "sim/simulation_kernel.h"
#include "net/net_service_stub.h"
#include "script/script_host_stub.h"
#include "world/world_service_basic.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace novaria::app {

class GameApp final {
public:
    GameApp();

    bool Initialize(const std::filesystem::path& config_path);
    int Run();
    void Shutdown();

private:
    bool initialized_ = false;
    bool quit_requested_ = false;
    core::GameConfig config_;
    platform::SdlContext sdl_context_;
    save::FileSaveRepository save_repository_;
    std::filesystem::path save_root_ = "saves";
    mod::ModLoader mod_loader_;
    std::filesystem::path mod_root_ = "mods";
    std::vector<mod::ModManifest> loaded_mods_;
    std::string mod_manifest_fingerprint_;
    platform::InputActions frame_actions_;
    std::uint32_t local_player_id_ = 1;
    std::uint64_t script_ping_counter_ = 0;
    world::WorldServiceBasic world_service_;
    net::NetServiceStub net_service_;
    script::ScriptHostStub script_host_;
    sim::SimulationKernel simulation_kernel_;
};

}  // namespace novaria::app
