#include "script/script_host_runtime.h"

#include "core/logger.h"

namespace novaria::script {

const char* ScriptBackendKindName(ScriptBackendKind backend_kind) {
    switch (backend_kind) {
        case ScriptBackendKind::None:
            return "none";
        case ScriptBackendKind::Stub:
            return "stub";
        case ScriptBackendKind::LuaJit:
            return "luajit";
    }

    return "unknown";
}

const char* ScriptBackendPreferenceName(ScriptBackendPreference preference) {
    switch (preference) {
        case ScriptBackendPreference::Auto:
            return "auto";
        case ScriptBackendPreference::Stub:
            return "stub";
        case ScriptBackendPreference::LuaJit:
            return "luajit";
    }

    return "unknown";
}

void ScriptHostRuntime::SetBackendPreference(ScriptBackendPreference preference) {
    if (backend_preference_ == preference) {
        return;
    }

    Shutdown();
    backend_preference_ = preference;
}

ScriptBackendPreference ScriptHostRuntime::BackendPreference() const {
    return backend_preference_;
}

ScriptBackendKind ScriptHostRuntime::ActiveBackend() const {
    return active_backend_;
}

const std::string& ScriptHostRuntime::LastBackendError() const {
    return last_backend_error_;
}

bool ScriptHostRuntime::Initialize(std::string& out_error) {
    Shutdown();
    last_backend_error_.clear();

    if (backend_preference_ == ScriptBackendPreference::Stub) {
        return InitializeWithStub(out_error);
    }

    if (backend_preference_ == ScriptBackendPreference::LuaJit) {
        return InitializeWithLuaJit(out_error);
    }

    std::string lua_error;
    if (InitializeWithLuaJit(lua_error)) {
        out_error.clear();
        return true;
    }

    last_backend_error_ = lua_error;
    core::Logger::Warn(
        "script",
        "LuaJIT unavailable, fallback to stub backend: " + lua_error);

    if (!InitializeWithStub(out_error)) {
        return false;
    }

    out_error.clear();
    return true;
}

void ScriptHostRuntime::Shutdown() {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->Shutdown();
    active_host_ = nullptr;
    active_backend_ = ScriptBackendKind::None;
}

void ScriptHostRuntime::Tick(const sim::TickContext& tick_context) {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->Tick(tick_context);
}

void ScriptHostRuntime::DispatchEvent(const ScriptEvent& event_data) {
    if (active_host_ == nullptr) {
        return;
    }

    active_host_->DispatchEvent(event_data);
}

ScriptRuntimeDescriptor ScriptHostRuntime::RuntimeDescriptor() const {
    if (active_host_ != nullptr) {
        return active_host_->RuntimeDescriptor();
    }

    return ScriptRuntimeDescriptor{
        .backend_name = ScriptBackendKindName(active_backend_),
        .api_version = kScriptApiVersion,
        .sandbox_enabled = false,
    };
}

bool ScriptHostRuntime::InitializeWithStub(std::string& out_error) {
    if (!stub_host_.Initialize(out_error)) {
        active_host_ = nullptr;
        active_backend_ = ScriptBackendKind::None;
        return false;
    }

    active_host_ = &stub_host_;
    active_backend_ = ScriptBackendKind::Stub;
    core::Logger::Info("script", "Script runtime backend: stub.");
    return true;
}

bool ScriptHostRuntime::InitializeWithLuaJit(std::string& out_error) {
    if (!lua_jit_host_.Initialize(out_error)) {
        active_host_ = nullptr;
        active_backend_ = ScriptBackendKind::None;
        return false;
    }

    active_host_ = &lua_jit_host_;
    active_backend_ = ScriptBackendKind::LuaJit;
    core::Logger::Info("script", "Script runtime backend: luajit.");
    return true;
}

}  // namespace novaria::script
