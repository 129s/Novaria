#include "script/lua_jit_script_host.h"

#include "core/logger.h"

#include <algorithm>
#include <string>

#if defined(NOVARIA_WITH_LUAJIT)
#include <cstring>
#include <cstdlib>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace novaria::script {
namespace {

constexpr const char* kBootstrapScript = R"lua(
novaria = novaria or {}
novaria.last_tick = 0
novaria.last_delta = 0
novaria.last_event_name = ""
novaria.last_event_payload = ""

function novaria_on_tick(tick_index, delta_seconds)
  novaria.last_tick = tick_index
  novaria.last_delta = delta_seconds
end

function novaria_on_event(event_name, payload)
  novaria.last_event_name = event_name
  novaria.last_event_payload = payload
end
)lua";

#if defined(NOVARIA_WITH_LUAJIT)
std::string ReadLuaError(lua_State* lua_state) {
    const char* error_message = lua_tostring(lua_state, -1);
    std::string output = error_message != nullptr ? error_message : "unknown LuaJIT error";
    lua_pop(lua_state, 1);
    return output;
}

void ClearGlobal(lua_State* lua_state, const char* global_name) {
    lua_pushnil(lua_state);
    lua_setglobal(lua_state, global_name);
}

void InstructionBudgetHook(lua_State* lua_state, lua_Debug* debug) {
    (void)debug;
    luaL_error(lua_state, "instruction budget exceeded");
}

bool RunProtectedLuaCall(
    lua_State* lua_state,
    int instruction_budget_per_call,
    int argument_count,
    std::string& out_error) {
    lua_sethook(lua_state, InstructionBudgetHook, LUA_MASKCOUNT, instruction_budget_per_call);
    const int run_status = lua_pcall(lua_state, argument_count, 0, 0);
    lua_sethook(lua_state, nullptr, 0, 0);
    if (run_status != LUA_OK) {
        out_error = ReadLuaError(lua_state);
        return false;
    }

    out_error.clear();
    return true;
}
#endif

}  // namespace

void* LuaJitScriptHost::QuotaAllocator(
    void* user_data,
    void* pointer,
    size_t old_size,
    size_t new_size) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)user_data;
    (void)pointer;
    (void)old_size;
    (void)new_size;
    return nullptr;
#else
    auto* quota_state = static_cast<MemoryQuotaState*>(user_data);
    if (quota_state == nullptr) {
        return nullptr;
    }

    if (new_size == 0) {
        if (pointer != nullptr) {
            std::free(pointer);
        }
        if (old_size <= quota_state->bytes_in_use) {
            quota_state->bytes_in_use -= old_size;
        } else {
            quota_state->bytes_in_use = 0;
        }
        return nullptr;
    }

    if (new_size > old_size) {
        const std::size_t growth_size = new_size - old_size;
        if (quota_state->bytes_in_use + growth_size > quota_state->limit_bytes) {
            return nullptr;
        }
    }

    void* new_pointer = std::realloc(pointer, new_size);
    if (new_pointer == nullptr) {
        return nullptr;
    }

    if (new_size >= old_size) {
        quota_state->bytes_in_use += (new_size - old_size);
    } else {
        quota_state->bytes_in_use -= (old_size - new_size);
    }

    return new_pointer;
#endif
}

bool LuaJitScriptHost::LoadScriptModules(
    const std::vector<ScriptModuleSource>& module_sources,
    std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)module_sources;
    out_error = "LuaJIT support is disabled at build time.";
    return false;
#else
    if (!initialized_ || lua_state_ == nullptr) {
        out_error = "LuaJIT script host is not initialized.";
        return false;
    }

    ClearLoadedModules();
    for (const auto& module_source : module_sources) {
        if (!LoadModuleScript(module_source, out_error)) {
            ClearLoadedModules();
            return false;
        }
    }

    out_error.clear();
    return true;
#endif
}

bool LuaJitScriptHost::Initialize(std::string& out_error) {
    pending_events_.clear();
    loaded_modules_.clear();
    total_processed_event_count_ = 0;
    dropped_event_count_ = 0;

#if !defined(NOVARIA_WITH_LUAJIT)
    initialized_ = false;
    lua_state_ = nullptr;
    out_error = "LuaJIT support is disabled at build time.";
    return false;
#else
    memory_quota_state_.bytes_in_use = 0;
    memory_quota_state_.limit_bytes = kMemoryBudgetBytes;
    lua_state_ = lua_newstate(QuotaAllocator, &memory_quota_state_);
    if (lua_state_ == nullptr) {
        initialized_ = false;
        out_error =
            "lua_newstate failed (memory budget=" +
            std::to_string(memory_quota_state_.limit_bytes) + ").";
        return false;
    }

    luaL_openlibs(lua_state_);
    if (!ApplyMvpSandbox(out_error)) {
        lua_close(lua_state_);
        lua_state_ = nullptr;
        initialized_ = false;
        return false;
    }
    if (!LoadBootstrapScript(out_error)) {
        lua_close(lua_state_);
        lua_state_ = nullptr;
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    out_error.clear();
    core::Logger::Info("script", "LuaJIT script host initialized.");
    return true;
#endif
}

void LuaJitScriptHost::Shutdown() {
    if (!initialized_) {
        return;
    }

#if defined(NOVARIA_WITH_LUAJIT)
    if (lua_state_ != nullptr) {
        ClearLoadedModules();
        lua_close(lua_state_);
        lua_state_ = nullptr;
    }
#endif

    pending_events_.clear();
    loaded_modules_.clear();
    memory_quota_state_.bytes_in_use = 0;
    initialized_ = false;
    core::Logger::Info("script", "LuaJIT script host shutdown.");
}

void LuaJitScriptHost::Tick(const sim::TickContext& tick_context) {
    if (!initialized_) {
        return;
    }

#if !defined(NOVARIA_WITH_LUAJIT)
    (void)tick_context;
#endif

#if defined(NOVARIA_WITH_LUAJIT)
    std::string tick_error;
    for (const LoadedModule& module : loaded_modules_) {
        if (!InvokeModuleTickHandler(module, tick_context, tick_error)) {
            core::Logger::Warn(
                "script",
                "LuaJIT tick handler failed (" + module.module_name + "): " + tick_error);
        }
    }

    std::string event_error;
    for (const ScriptEvent& event_data : pending_events_) {
        for (const LoadedModule& module : loaded_modules_) {
            if (!InvokeModuleEventHandler(module, event_data, event_error)) {
                core::Logger::Warn(
                    "script",
                    "LuaJIT event handler failed (" + module.module_name + "): " +
                        event_error);
            }
        }
    }
#endif

    total_processed_event_count_ += pending_events_.size();
    pending_events_.clear();
}

void LuaJitScriptHost::DispatchEvent(const ScriptEvent& event_data) {
    if (!initialized_) {
        return;
    }

    if (pending_events_.size() >= kMaxPendingEvents) {
        ++dropped_event_count_;
        return;
    }

    pending_events_.push_back(event_data);
}

ScriptRuntimeDescriptor LuaJitScriptHost::RuntimeDescriptor() const {
    std::size_t active_tick_handler_count = 0;
    std::size_t active_event_handler_count = 0;
    for (const LoadedModule& module : loaded_modules_) {
        if (module.can_receive_tick && module.has_tick_handler) {
            ++active_tick_handler_count;
        }
        if (module.can_receive_event && module.has_event_handler) {
            ++active_event_handler_count;
        }
    }

    return ScriptRuntimeDescriptor{
        .backend_name = "luajit",
        .api_version = kScriptApiVersion,
        .sandbox_enabled = true,
        .sandbox_level = "resource_limited",
        .memory_budget_bytes = kMemoryBudgetBytes,
        .instruction_budget_per_call = kInstructionBudgetPerCall,
        .loaded_module_count = loaded_modules_.size(),
        .active_tick_handler_count = active_tick_handler_count,
        .active_event_handler_count = active_event_handler_count,
    };
}

void LuaJitScriptHost::ClearLoadedModules() {
#if !defined(NOVARIA_WITH_LUAJIT)
    loaded_modules_.clear();
#else
    if (lua_state_ != nullptr) {
        for (const LoadedModule& module : loaded_modules_) {
            if (module.environment_ref == LUA_REFNIL ||
                module.environment_ref == LUA_NOREF) {
                continue;
            }

            luaL_unref(lua_state_, LUA_REGISTRYINDEX, module.environment_ref);
        }
    }

    loaded_modules_.clear();
#endif
}

bool LuaJitScriptHost::IsVmReady() const {
    return initialized_ && lua_state_ != nullptr;
}

std::size_t LuaJitScriptHost::PendingEventCount() const {
    return pending_events_.size();
}

std::size_t LuaJitScriptHost::TotalProcessedEventCount() const {
    return total_processed_event_count_;
}

std::size_t LuaJitScriptHost::DroppedEventCount() const {
    return dropped_event_count_;
}

bool LuaJitScriptHost::ApplyMvpSandbox(std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr) {
        out_error = "Lua state is null.";
        return false;
    }

    ClearGlobal(lua_state_, "io");
    ClearGlobal(lua_state_, "os");
    ClearGlobal(lua_state_, "debug");
    ClearGlobal(lua_state_, "package");
    ClearGlobal(lua_state_, "dofile");
    ClearGlobal(lua_state_, "loadfile");
    ClearGlobal(lua_state_, "load");
    ClearGlobal(lua_state_, "require");
    ClearGlobal(lua_state_, "collectgarbage");
    ClearGlobal(lua_state_, "jit");

    lua_getglobal(lua_state_, "string");
    if (lua_istable(lua_state_, -1)) {
        lua_pushnil(lua_state_);
        lua_setfield(lua_state_, -2, "dump");
    }
    lua_pop(lua_state_, 1);

    out_error.clear();
    return true;
#endif
}

bool LuaJitScriptHost::LoadBootstrapScript(std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr) {
        out_error = "Lua state is null.";
        return false;
    }

    const int load_status = luaL_loadbufferx(
        lua_state_,
        kBootstrapScript,
        std::strlen(kBootstrapScript),
        "novaria_bootstrap",
        nullptr);
    if (load_status != LUA_OK) {
        out_error = "Failed to compile bootstrap script: " + ReadLuaError(lua_state_);
        return false;
    }

    if (!RunProtectedLuaCall(
            lua_state_,
            static_cast<int>(kInstructionBudgetPerCall),
            0,
            out_error)) {
        out_error = "Failed to run bootstrap script: " + out_error;
        return false;
    }

    out_error.clear();
    return true;
#endif
}

bool LuaJitScriptHost::BindModuleEnvironment(
    int& out_environment_ref,
    std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)out_environment_ref;
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr) {
        out_error = "Lua state is null.";
        return false;
    }

    if (!lua_isfunction(lua_state_, -1)) {
        out_error = "Module chunk is not on stack.";
        return false;
    }

    lua_newtable(lua_state_);
    lua_pushvalue(lua_state_, -1);
    lua_setfield(lua_state_, -2, "_G");

    lua_newtable(lua_state_);
    lua_pushvalue(lua_state_, LUA_GLOBALSINDEX);
    lua_setfield(lua_state_, -2, "__index");
    lua_setmetatable(lua_state_, -2);

    lua_pushvalue(lua_state_, -1);
    if (lua_setfenv(lua_state_, -3) == 0) {
        lua_pop(lua_state_, 1);
        out_error = "Failed to bind module environment.";
        return false;
    }

    out_environment_ref = luaL_ref(lua_state_, LUA_REGISTRYINDEX);
    out_error.clear();
    return true;
#endif
}

void LuaJitScriptHost::DetectModuleHandlers(
    int environment_ref,
    bool& out_has_tick_handler,
    bool& out_has_event_handler) const {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)environment_ref;
    out_has_tick_handler = false;
    out_has_event_handler = false;
#else
    out_has_tick_handler = false;
    out_has_event_handler = false;
    if (lua_state_ == nullptr || environment_ref == LUA_REFNIL ||
        environment_ref == LUA_NOREF) {
        return;
    }

    lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, environment_ref);
    if (!lua_istable(lua_state_, -1)) {
        lua_pop(lua_state_, 1);
        return;
    }

    lua_getfield(lua_state_, -1, "novaria_on_tick");
    out_has_tick_handler = lua_isfunction(lua_state_, -1);
    lua_pop(lua_state_, 1);

    lua_getfield(lua_state_, -1, "novaria_on_event");
    out_has_event_handler = lua_isfunction(lua_state_, -1);
    lua_pop(lua_state_, 1);

    lua_pop(lua_state_, 1);
#endif
}

bool LuaJitScriptHost::LoadModuleScript(
    const ScriptModuleSource& module_source,
    std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)module_source;
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr) {
        out_error = "Lua state is null.";
        return false;
    }

    const int load_status = luaL_loadbufferx(
        lua_state_,
        module_source.source_code.c_str(),
        module_source.source_code.size(),
        module_source.module_name.c_str(),
        nullptr);
    if (load_status != LUA_OK) {
        out_error =
            "Failed to compile module '" + module_source.module_name + "': " +
            ReadLuaError(lua_state_);
        return false;
    }

    int environment_ref = LUA_REFNIL;
    if (!BindModuleEnvironment(environment_ref, out_error)) {
        lua_pop(lua_state_, 1);
        out_error =
            "Failed to isolate module '" + module_source.module_name +
            "': " + out_error;
        return false;
    }

    if (!RunProtectedLuaCall(
            lua_state_,
            static_cast<int>(kInstructionBudgetPerCall),
            0,
            out_error)) {
        luaL_unref(lua_state_, LUA_REGISTRYINDEX, environment_ref);
        out_error =
            "Failed to run module '" + module_source.module_name + "': " + out_error;
        if (out_error.find("memory") != std::string::npos) {
            out_error +=
                " (usage=" + std::to_string(memory_quota_state_.bytes_in_use) +
                "/" + std::to_string(memory_quota_state_.limit_bytes) + ")";
        }
        return false;
    }

    bool has_tick_handler = false;
    bool has_event_handler = false;
    DetectModuleHandlers(environment_ref, has_tick_handler, has_event_handler);
    const bool can_receive_tick =
        std::find(
            module_source.capabilities.begin(),
            module_source.capabilities.end(),
            "tick.receive") != module_source.capabilities.end();
    const bool can_receive_event =
        std::find(
            module_source.capabilities.begin(),
            module_source.capabilities.end(),
            "event.receive") != module_source.capabilities.end();
    loaded_modules_.push_back(LoadedModule{
        .module_name = module_source.module_name,
        .environment_ref = environment_ref,
        .can_receive_tick = can_receive_tick,
        .can_receive_event = can_receive_event,
        .has_tick_handler = has_tick_handler,
        .has_event_handler = has_event_handler,
    });

    out_error.clear();
    return true;
#endif
}

bool LuaJitScriptHost::InvokeModuleTickHandler(
    const LoadedModule& module,
    const sim::TickContext& tick_context,
    std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)module;
    (void)tick_context;
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr || !module.can_receive_tick ||
        !module.has_tick_handler || module.environment_ref == LUA_REFNIL ||
        module.environment_ref == LUA_NOREF) {
        out_error.clear();
        return true;
    }

    lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, module.environment_ref);
    if (!lua_istable(lua_state_, -1)) {
        lua_pop(lua_state_, 1);
        out_error.clear();
        return true;
    }

    lua_getfield(lua_state_, -1, "novaria_on_tick");
    if (!lua_isfunction(lua_state_, -1)) {
        lua_pop(lua_state_, 2);
        out_error.clear();
        return true;
    }

    lua_pushinteger(lua_state_, static_cast<lua_Integer>(tick_context.tick_index));
    lua_pushnumber(lua_state_, static_cast<lua_Number>(tick_context.fixed_delta_seconds));
    if (!RunProtectedLuaCall(
            lua_state_,
            static_cast<int>(kInstructionBudgetPerCall),
            2,
            out_error)) {
        lua_pop(lua_state_, 1);
        return false;
    }

    lua_pop(lua_state_, 1);
    out_error.clear();
    return true;
#endif
}

bool LuaJitScriptHost::InvokeModuleEventHandler(
    const LoadedModule& module,
    const ScriptEvent& event_data,
    std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)module;
    (void)event_data;
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr || !module.can_receive_event ||
        !module.has_event_handler || module.environment_ref == LUA_REFNIL ||
        module.environment_ref == LUA_NOREF) {
        out_error.clear();
        return true;
    }

    lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, module.environment_ref);
    if (!lua_istable(lua_state_, -1)) {
        lua_pop(lua_state_, 1);
        out_error.clear();
        return true;
    }

    lua_getfield(lua_state_, -1, "novaria_on_event");
    if (!lua_isfunction(lua_state_, -1)) {
        lua_pop(lua_state_, 2);
        out_error.clear();
        return true;
    }

    lua_pushstring(lua_state_, event_data.event_name.c_str());
    lua_pushstring(lua_state_, event_data.payload.c_str());
    if (!RunProtectedLuaCall(
            lua_state_,
            static_cast<int>(kInstructionBudgetPerCall),
            2,
            out_error)) {
        lua_pop(lua_state_, 1);
        return false;
    }

    lua_pop(lua_state_, 1);
    out_error.clear();
    return true;
#endif
}

}  // namespace novaria::script
