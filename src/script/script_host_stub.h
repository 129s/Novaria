#pragma once

#include "script/script_host.h"

#include <cstddef>
#include <vector>

namespace novaria::script {

class ScriptHostStub final : public IScriptHost {
public:
    static constexpr std::size_t kMaxPendingEvents = 1024;

    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void DispatchEvent(const ScriptEvent& event_data) override;
    ScriptRuntimeDescriptor RuntimeDescriptor() const override;

    std::size_t PendingEventCount() const;
    std::size_t TotalProcessedEventCount() const;
    std::size_t DroppedEventCount() const;

private:
    bool initialized_ = false;
    std::vector<ScriptEvent> pending_events_;
    std::size_t total_processed_event_count_ = 0;
    std::size_t dropped_event_count_ = 0;
};

}  // namespace novaria::script
