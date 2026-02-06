#include "script/script_host_stub.h"

#include "core/logger.h"

namespace novaria::script {

bool ScriptHostStub::Initialize(std::string& out_error) {
    pending_events_.clear();
    total_processed_event_count_ = 0;
    initialized_ = true;
    out_error.clear();
    core::Logger::Info("script", "Script host stub initialized.");
    return true;
}

void ScriptHostStub::Shutdown() {
    if (!initialized_) {
        return;
    }

    pending_events_.clear();
    initialized_ = false;
    core::Logger::Info("script", "Script host stub shutdown.");
}

void ScriptHostStub::Tick(const sim::TickContext& tick_context) {
    (void)tick_context;
    if (!initialized_) {
        return;
    }

    total_processed_event_count_ += pending_events_.size();
    pending_events_.clear();
}

void ScriptHostStub::DispatchEvent(const ScriptEvent& event_data) {
    if (!initialized_) {
        return;
    }

    pending_events_.push_back(event_data);
}

std::size_t ScriptHostStub::PendingEventCount() const {
    return pending_events_.size();
}

std::size_t ScriptHostStub::TotalProcessedEventCount() const {
    return total_processed_event_count_;
}

}  // namespace novaria::script
