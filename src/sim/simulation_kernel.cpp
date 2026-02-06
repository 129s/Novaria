#include "sim/simulation_kernel.h"

namespace novaria::sim {

SimulationKernel::SimulationKernel(
    world::IWorldService& world_service,
    net::INetService& net_service,
    script::IScriptHost& script_host)
    : world_service_(world_service), net_service_(net_service), script_host_(script_host) {}

bool SimulationKernel::Initialize(std::string& out_error) {
    std::string dependency_error;
    if (!world_service_.Initialize(dependency_error)) {
        out_error = "World service initialize failed: " + dependency_error;
        return false;
    }

    if (!net_service_.Initialize(dependency_error)) {
        world_service_.Shutdown();
        out_error = "Net service initialize failed: " + dependency_error;
        return false;
    }

    if (!script_host_.Initialize(dependency_error)) {
        net_service_.Shutdown();
        world_service_.Shutdown();
        out_error = "Script host initialize failed: " + dependency_error;
        return false;
    }

    tick_index_ = 0;
    initialized_ = true;
    out_error.clear();
    return true;
}

void SimulationKernel::Shutdown() {
    if (!initialized_) {
        return;
    }

    script_host_.Shutdown();
    net_service_.Shutdown();
    world_service_.Shutdown();
    initialized_ = false;
}

void SimulationKernel::Update(double fixed_delta_seconds) {
    if (!initialized_) {
        return;
    }

    const TickContext tick_context{
        .tick_index = tick_index_,
        .fixed_delta_seconds = fixed_delta_seconds,
    };

    net_service_.Tick(tick_context);
    world_service_.Tick(tick_context);
    script_host_.Tick(tick_context);
    const auto dirty_chunks = world_service_.ConsumeDirtyChunks();
    net_service_.PublishWorldSnapshot(tick_index_, dirty_chunks.size());

    ++tick_index_;
}

}  // namespace novaria::sim
