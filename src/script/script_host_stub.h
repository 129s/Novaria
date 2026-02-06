#pragma once

#include "script/script_host.h"

#include <cstddef>
#include <vector>

namespace novaria::script {

class ScriptHostStub final : public IScriptHost {
public:
    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void DispatchEvent(const ScriptEvent& event_data) override;

    std::size_t PendingEventCount() const;
    std::size_t TotalProcessedEventCount() const;

private:
    bool initialized_ = false;
    std::vector<ScriptEvent> pending_events_;
    std::size_t total_processed_event_count_ = 0;
};

}  // namespace novaria::script
