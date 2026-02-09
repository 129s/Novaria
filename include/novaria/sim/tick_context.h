#pragma once

#include <cstdint>

namespace novaria::core {

struct TickContext final {
    std::uint64_t tick_index = 0;
    double fixed_delta_seconds = 0.0;
};

}  // namespace novaria::core

