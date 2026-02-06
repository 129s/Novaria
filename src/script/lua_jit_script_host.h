#pragma once

#include "script/script_host.h"

#include <cstddef>
#include <vector>

struct lua_State;

namespace novaria::script {

class LuaJitScriptHost final : public IScriptHost {
public:
    static constexpr std::size_t kMaxPendingEvents = 1024;

    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void DispatchEvent(const ScriptEvent& event_data) override;
    ScriptRuntimeDescriptor RuntimeDescriptor() const override;

    bool IsVmReady() const;
    std::size_t PendingEventCount() const;
    std::size_t TotalProcessedEventCount() const;
    std::size_t DroppedEventCount() const;

private:
    bool LoadBootstrapScript(std::string& out_error);
    bool InvokeTickHandler(const sim::TickContext& tick_context, std::string& out_error);
    bool InvokeEventHandler(const ScriptEvent& event_data, std::string& out_error);

    bool initialized_ = false;
    lua_State* lua_state_ = nullptr;
    std::vector<ScriptEvent> pending_events_;
    std::size_t total_processed_event_count_ = 0;
    std::size_t dropped_event_count_ = 0;
};

}  // namespace novaria::script
