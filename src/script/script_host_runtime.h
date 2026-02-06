#pragma once

#include "script/lua_jit_script_host.h"
#include "script/script_host.h"

#include <string>

namespace novaria::script {

enum class ScriptBackendPreference {
    LuaJit,
};

enum class ScriptBackendKind {
    None,
    LuaJit,
};

const char* ScriptBackendKindName(ScriptBackendKind backend_kind);
const char* ScriptBackendPreferenceName(ScriptBackendPreference preference);

class ScriptHostRuntime final : public IScriptHost {
public:
    void SetBackendPreference(ScriptBackendPreference preference);
    ScriptBackendPreference BackendPreference() const;
    ScriptBackendKind ActiveBackend() const;
    const std::string& LastBackendError() const;

    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void DispatchEvent(const ScriptEvent& event_data) override;
    ScriptRuntimeDescriptor RuntimeDescriptor() const override;

private:
    bool InitializeWithLuaJit(std::string& out_error);

    ScriptBackendPreference backend_preference_ = ScriptBackendPreference::LuaJit;
    ScriptBackendKind active_backend_ = ScriptBackendKind::None;
    IScriptHost* active_host_ = nullptr;
    std::string last_backend_error_;
    LuaJitScriptHost lua_jit_host_;
};

}  // namespace novaria::script
