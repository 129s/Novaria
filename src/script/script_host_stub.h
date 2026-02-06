#pragma once

#include "script/script_host.h"

namespace novaria::script {

class ScriptHostStub final : public IScriptHost {
public:
    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void DispatchEvent(const ScriptEvent& event_data) override;

private:
    bool initialized_ = false;
};

}  // namespace novaria::script
