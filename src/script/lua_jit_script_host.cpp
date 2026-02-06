#include "script/lua_jit_script_host.h"

#include "core/logger.h"

#include <string>

#if defined(NOVARIA_WITH_LUAJIT)
#include <cstring>

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
#endif

}  // namespace

bool LuaJitScriptHost::Initialize(std::string& out_error) {
    pending_events_.clear();
    total_processed_event_count_ = 0;
    dropped_event_count_ = 0;

#if !defined(NOVARIA_WITH_LUAJIT)
    initialized_ = false;
    lua_state_ = nullptr;
    out_error = "LuaJIT support is disabled at build time.";
    return false;
#else
    lua_state_ = luaL_newstate();
    if (lua_state_ == nullptr) {
        initialized_ = false;
        out_error = "luaL_newstate failed.";
        return false;
    }

    luaL_openlibs(lua_state_);
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
        lua_close(lua_state_);
        lua_state_ = nullptr;
    }
#endif

    pending_events_.clear();
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
    if (!InvokeTickHandler(tick_context, tick_error)) {
        core::Logger::Warn("script", "LuaJIT tick handler failed: " + tick_error);
    }

    std::string event_error;
    for (const auto& event_data : pending_events_) {
        if (!InvokeEventHandler(event_data, event_error)) {
            core::Logger::Warn("script", "LuaJIT event handler failed: " + event_error);
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

    const int run_status = lua_pcall(lua_state_, 0, 0, 0);
    if (run_status != LUA_OK) {
        out_error = "Failed to run bootstrap script: " + ReadLuaError(lua_state_);
        return false;
    }

    out_error.clear();
    return true;
#endif
}

bool LuaJitScriptHost::InvokeTickHandler(const sim::TickContext& tick_context, std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)tick_context;
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr) {
        out_error = "Lua state is null.";
        return false;
    }

    lua_getglobal(lua_state_, "novaria_on_tick");
    if (!lua_isfunction(lua_state_, -1)) {
        lua_pop(lua_state_, 1);
        return true;
    }

    lua_pushinteger(lua_state_, static_cast<lua_Integer>(tick_context.tick_index));
    lua_pushnumber(lua_state_, static_cast<lua_Number>(tick_context.fixed_delta_seconds));
    if (lua_pcall(lua_state_, 2, 0, 0) != LUA_OK) {
        out_error = ReadLuaError(lua_state_);
        return false;
    }

    out_error.clear();
    return true;
#endif
}

bool LuaJitScriptHost::InvokeEventHandler(const ScriptEvent& event_data, std::string& out_error) {
#if !defined(NOVARIA_WITH_LUAJIT)
    (void)event_data;
    (void)out_error;
    return false;
#else
    if (lua_state_ == nullptr) {
        out_error = "Lua state is null.";
        return false;
    }

    lua_getglobal(lua_state_, "novaria_on_event");
    if (!lua_isfunction(lua_state_, -1)) {
        lua_pop(lua_state_, 1);
        return true;
    }

    lua_pushstring(lua_state_, event_data.event_name.c_str());
    lua_pushstring(lua_state_, event_data.payload.c_str());
    if (lua_pcall(lua_state_, 2, 0, 0) != LUA_OK) {
        out_error = ReadLuaError(lua_state_);
        return false;
    }

    out_error.clear();
    return true;
#endif
}

}  // namespace novaria::script
