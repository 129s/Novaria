#include "app/game_loop.h"

#include <algorithm>
#include <chrono>

namespace novaria::app {

void GameLoop::Run(const PumpEventsFn& pump_events, const UpdateFn& update, const RenderFn& render) {
    constexpr double kFixedDeltaSeconds = 1.0 / 60.0;
    constexpr double kMaxFrameClampSeconds = 0.25;

    auto previous_time = std::chrono::steady_clock::now();
    double accumulator = 0.0;

    while (pump_events()) {
        const auto now = std::chrono::steady_clock::now();
        auto frame_seconds =
            std::chrono::duration<double>(now - previous_time).count();
        previous_time = now;

        frame_seconds = std::clamp(frame_seconds, 0.0, kMaxFrameClampSeconds);
        accumulator += frame_seconds;

        while (accumulator >= kFixedDeltaSeconds) {
            update(kFixedDeltaSeconds);
            accumulator -= kFixedDeltaSeconds;
        }

        const float interpolation_alpha = static_cast<float>(accumulator / kFixedDeltaSeconds);
        render(interpolation_alpha);
    }
}

}  // namespace novaria::app
