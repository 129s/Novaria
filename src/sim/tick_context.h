#pragma once

#include <cstdint>

namespace novaria::sim {

struct TickContext final {
    std::uint64_t tick_index = 0;
    double fixed_delta_seconds = 0.0;
};

}  // namespace novaria::sim
