#include "script/script_host_runtime.h"

#include "core/logger.h"

namespace novaria::script {

const char* ScriptBackendKindName(ScriptBackendKind backend_kind) {
    switch (backend_kind) {
        case ScriptBackendKind::None:
            return "none";
        case ScriptBackendKind::LuaJit:
            return "luajit";
    }

    return "unknown";
}

const char* ScriptBackendPreferenceName(ScriptBackendPreference preference) {
    switch (preference) {
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

    if (!InitializeWithLuaJit(out_error)) {
        last_backend_error_ = out_error;
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
        .backend_name = ScriptBackendPreferenceName(backend_preference_),
        .api_version = kScriptApiVersion,
        .sandbox_enabled = false,
    };
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
