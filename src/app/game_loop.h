#pragma once

#include <functional>

namespace novaria::app {

class GameLoop final {
public:
    using PumpEventsFn = std::function<bool()>;
    using UpdateFn = std::function<void(double)>;
    using RenderFn = std::function<void(float)>;

    void Run(const PumpEventsFn& pump_events, const UpdateFn& update, const RenderFn& render);
};

}  // namespace novaria::app
