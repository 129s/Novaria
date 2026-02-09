#pragma once

#include "save/save_repository.h"
#include "sim/simulation_kernel.h"
#include "world/world_service.h"

#include <cstdint>
#include <string>

namespace novaria::runtime {

struct SaveLoadResult final {
    bool has_state = false;
    std::uint32_t local_player_id = 1;
    save::WorldSaveState state{};
};

bool TryLoadSaveState(
    save::ISaveRepository& repository,
    SaveLoadResult& out_result,
    std::string& out_error);

bool ApplySaveState(
    const save::WorldSaveState& loaded_state,
    sim::SimulationKernel& kernel,
    world::IWorldService& world_service);

}  // namespace novaria::runtime
