#pragma once

<<<<<<< HEAD
#include "core/tick_context.h"
=======
#include "sim/tick_context.h"
>>>>>>> 77c2e72a388234fbfa90639e804362c787d0e052
#include "wire/byte_io.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::script {

inline constexpr const char* kScriptApiVersion = "0.1.0";

struct ScriptEvent final {
    std::string event_name;
    std::string payload;
};

struct ScriptModuleSource final {
    std::string module_name;
    std::string api_version = kScriptApiVersion;
    std::vector<std::string> capabilities;
    std::string source_code;
};

struct ScriptRuntimeDescriptor final {
    std::string backend_name = "unknown";
    std::string api_version = kScriptApiVersion;
    bool sandbox_enabled = false;
    std::string sandbox_level = "none";
    std::uint64_t memory_budget_bytes = 0;
    std::uint64_t instruction_budget_per_call = 0;
    std::size_t loaded_module_count = 0;
    std::size_t active_tick_handler_count = 0;
    std::size_t active_event_handler_count = 0;
};

class IScriptHost {
public:
    virtual ~IScriptHost() = default;

    virtual bool SetScriptModules(
        std::vector<ScriptModuleSource> module_sources,
        std::string& out_error) = 0;
    virtual bool Initialize(std::string& out_error) = 0;
    virtual void Shutdown() = 0;
    virtual void Tick(const core::TickContext& tick_context) = 0;
    virtual void DispatchEvent(const ScriptEvent& event_data) = 0;
    virtual bool TryCallModuleFunction(
        std::string_view module_name,
        std::string_view function_name,
        wire::ByteSpan request_payload,
        wire::ByteBuffer& out_response_payload,
        std::string& out_error) = 0;
    virtual ScriptRuntimeDescriptor RuntimeDescriptor() const = 0;
};

}  // namespace novaria::script
