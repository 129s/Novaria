#pragma once

#include "script/script_host.h"

#include <cstddef>
#include <string>
#include <vector>

struct lua_State;

namespace novaria::script {

class LuaJitScriptHost final : public IScriptHost {
public:
    static constexpr std::size_t kMaxPendingEvents = 1024;
    static constexpr std::size_t kInstructionBudgetPerCall = 200000;
    static constexpr std::size_t kMemoryBudgetBytes = 64 * 1024 * 1024;

    bool SetScriptModules(
        std::vector<ScriptModuleSource> module_sources,
        std::string& out_error) override;

    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const core::TickContext& tick_context) override;
    void DispatchEvent(const ScriptEvent& event_data) override;
    bool TryCallModuleFunction(
        std::string_view module_name,
        std::string_view function_name,
        wire::ByteSpan request_payload,
        wire::ByteBuffer& out_response_payload,
        std::string& out_error) override;
    ScriptRuntimeDescriptor RuntimeDescriptor() const override;

    bool IsVmReady() const;
    std::size_t PendingEventCount() const;
    std::size_t TotalProcessedEventCount() const;
    std::size_t DroppedEventCount() const;

private:
    struct LoadedModule final {
        std::string module_name;
        int environment_ref = -1;
        bool can_receive_tick = false;
        bool can_receive_event = false;
        bool has_tick_handler = false;
        bool has_event_handler = false;
    };

    struct MemoryQuotaState final {
        std::size_t bytes_in_use = 0;
        std::size_t limit_bytes = kMemoryBudgetBytes;
    };

    static void* QuotaAllocator(void* user_data, void* pointer, size_t old_size, size_t new_size);
    void ClearLoadedModules();
    bool LoadScriptModules(
        const std::vector<ScriptModuleSource>& module_sources,
        std::string& out_error);
    bool ApplyMvpSandbox(std::string& out_error);
    bool LoadBootstrapScript(std::string& out_error);
    bool BindModuleEnvironment(int& out_environment_ref, std::string& out_error);
    void DetectModuleHandlers(
        int environment_ref,
        bool& out_has_tick_handler,
        bool& out_has_event_handler) const;
    bool LoadModuleScript(
        const ScriptModuleSource& module_source,
        std::string& out_error);
    bool InvokeModuleTickHandler(
        const LoadedModule& module,
        const core::TickContext& tick_context,
        std::string& out_error);
    bool InvokeModuleEventHandler(
        const LoadedModule& module,
        const ScriptEvent& event_data,
        std::string& out_error);

    bool initialized_ = false;
    lua_State* lua_state_ = nullptr;
    std::vector<ScriptModuleSource> module_sources_;
    MemoryQuotaState memory_quota_state_{}; 
    std::vector<ScriptEvent> pending_events_;
    std::vector<LoadedModule> loaded_modules_;
    std::size_t total_processed_event_count_ = 0;
    std::size_t dropped_event_count_ = 0;
};

}  // namespace novaria::script
