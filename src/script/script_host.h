#pragma once

#include "sim/tick_context.h"

#include <string>

namespace novaria::script {

inline constexpr const char* kScriptApiVersion = "0.1.0";

struct ScriptEvent final {
    std::string event_name;
    std::string payload;
};

struct ScriptRuntimeDescriptor final {
    std::string backend_name = "unknown";
    std::string api_version = kScriptApiVersion;
    bool sandbox_enabled = false;
};

class IScriptHost {
public:
    virtual ~IScriptHost() = default;

    virtual bool Initialize(std::string& out_error) = 0;
    virtual void Shutdown() = 0;
    virtual void Tick(const sim::TickContext& tick_context) = 0;
    virtual void DispatchEvent(const ScriptEvent& event_data) = 0;
    virtual ScriptRuntimeDescriptor RuntimeDescriptor() const = 0;
};

}  // namespace novaria::script
