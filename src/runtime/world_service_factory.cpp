#include "runtime/world_service_factory.h"

#include "world/world_service_basic.h"

namespace novaria::runtime {

std::unique_ptr<world::IWorldService> CreateWorldService() {
    return std::make_unique<world::WorldServiceBasic>();
}

}  // namespace novaria::runtime
