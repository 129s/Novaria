#pragma once

#include "script/lua_jit_script_host.h"
#include "script/script_host.h"
#include "script/script_host_stub.h"

#include <string>

namespace novaria::script {

enum class ScriptBackendPreference {
    Auto,
    Stub,
    LuaJit,
};

enum class ScriptBackendKind {
    None,
    Stub,
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

private:
    bool InitializeWithStub(std::string& out_error);
    bool InitializeWithLuaJit(std::string& out_error);

    ScriptBackendPreference backend_preference_ = ScriptBackendPreference::Auto;
    ScriptBackendKind active_backend_ = ScriptBackendKind::None;
    IScriptHost* active_host_ = nullptr;
    std::string last_backend_error_;
    ScriptHostStub stub_host_;
    LuaJitScriptHost lua_jit_host_;
};

}  // namespace novaria::script
