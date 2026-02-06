#pragma once

#include "net/net_service.h"
#include "script/script_host.h"
#include "world/world_service.h"

#include <cstdint>
#include <string>
#include <vector>

namespace novaria::sim {

class SimulationKernel final {
public:
    SimulationKernel(
        world::IWorldService& world_service,
        net::INetService& net_service,
        script::IScriptHost& script_host);

    bool Initialize(std::string& out_error);
    void Shutdown();
    void SubmitLocalCommand(const net::PlayerCommand& command);
    void Update(double fixed_delta_seconds);

private:
    bool initialized_ = false;
    std::uint64_t tick_index_ = 0;
    world::IWorldService& world_service_;
    net::INetService& net_service_;
    script::IScriptHost& script_host_;
    std::vector<net::PlayerCommand> pending_local_commands_;
};

}  // namespace novaria::sim
