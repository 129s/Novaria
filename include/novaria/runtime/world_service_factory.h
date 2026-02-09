#pragma once

#include "world/world_service.h"

#include <memory>

namespace novaria::runtime {

std::unique_ptr<world::IWorldService> CreateWorldService();

}  // namespace novaria::runtime
