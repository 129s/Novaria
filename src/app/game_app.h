#pragma once

#include "core/config.h"
#include "platform/sdl_context.h"
#include "sim/simulation_kernel.h"
#include "net/net_service_stub.h"
#include "script/script_host_stub.h"
#include "world/world_service_basic.h"

#include <filesystem>

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
    world::WorldServiceBasic world_service_;
    net::NetServiceStub net_service_;
    script::ScriptHostStub script_host_;
    sim::SimulationKernel simulation_kernel_;
};

}  // namespace novaria::app
