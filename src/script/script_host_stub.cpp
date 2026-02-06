#include "script/script_host_stub.h"

#include "core/logger.h"

namespace novaria::script {

bool ScriptHostStub::Initialize(std::string& out_error) {
    initialized_ = true;
    out_error.clear();
    core::Logger::Info("script", "Script host stub initialized.");
    return true;
}

void ScriptHostStub::Shutdown() {
    if (!initialized_) {
        return;
    }

    initialized_ = false;
    core::Logger::Info("script", "Script host stub shutdown.");
}

void ScriptHostStub::Tick(const sim::TickContext& tick_context) {
    (void)tick_context;
}

void ScriptHostStub::DispatchEvent(const ScriptEvent& event_data) {
    (void)event_data;
}

}  // namespace novaria::script
