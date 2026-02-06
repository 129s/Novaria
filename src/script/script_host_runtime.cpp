#include "script/script_host_runtime.h"

#include "core/logger.h"

#include <unordered_set>
#include <utility>

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

bool ScriptHostRuntime::SetScriptModules(
    std::vector<ScriptModuleSource> module_sources,
    std::string& out_error) {
    std::unordered_set<std::string> unique_module_names;
    unique_module_names.reserve(module_sources.size());
    for (auto& module_source : module_sources) {
        if (module_source.module_name.empty()) {
            out_error = "Script module name cannot be empty.";
            return false;
        }

        if (!unique_module_names.emplace(module_source.module_name).second) {
            out_error = "Duplicate script module name: " + module_source.module_name;
            return false;
        }

        if (module_source.source_code.empty()) {
            out_error =
                "Script module source cannot be empty: " + module_source.module_name;
            return false;
        }

        if (module_source.api_version.empty()) {
            module_source.api_version = kScriptApiVersion;
        }

        if (module_source.api_version != kScriptApiVersion) {
            out_error =
                "Script module API version mismatch: module=" + module_source.module_name +
                ", required=" + module_source.api_version +
                ", runtime=" + std::string(kScriptApiVersion);
            return false;
        }
    }

    if (active_backend_ == ScriptBackendKind::LuaJit &&
        !lua_jit_host_.LoadScriptModules(module_sources, out_error)) {
        return false;
    }

    module_sources_ = std::move(module_sources);
    out_error.clear();
    return true;
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

    if (!module_sources_.empty() &&
        !lua_jit_host_.LoadScriptModules(module_sources_, out_error)) {
        lua_jit_host_.Shutdown();
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
