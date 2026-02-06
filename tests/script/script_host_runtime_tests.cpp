#include "script/script_host_runtime.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool passed = true;

    novaria::script::ScriptHostRuntime runtime;
    std::string error;

    runtime.SetBackendPreference(novaria::script::ScriptBackendPreference::Auto);
    passed &= Expect(runtime.Initialize(error), "Auto backend initialization should succeed.");
    passed &= Expect(error.empty(), "Auto backend init should not return error.");
    passed &= Expect(
        runtime.ActiveBackend() != novaria::script::ScriptBackendKind::None,
        "Runtime should choose an active backend.");
    runtime.DispatchEvent({.event_name = "runtime.auto.test", .payload = "payload"});
    runtime.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    runtime.Shutdown();

    runtime.SetBackendPreference(novaria::script::ScriptBackendPreference::Stub);
    passed &= Expect(runtime.Initialize(error), "Stub backend initialization should succeed.");
    passed &= Expect(
        runtime.ActiveBackend() == novaria::script::ScriptBackendKind::Stub,
        "Stub preference should select stub backend.");
    runtime.DispatchEvent({.event_name = "runtime.stub.test", .payload = "payload"});
    runtime.Tick({.tick_index = 2, .fixed_delta_seconds = 1.0 / 60.0});
    runtime.Shutdown();

    runtime.SetBackendPreference(novaria::script::ScriptBackendPreference::LuaJit);
    const bool luajit_init_ok = runtime.Initialize(error);
    if (luajit_init_ok) {
        passed &= Expect(
            runtime.ActiveBackend() == novaria::script::ScriptBackendKind::LuaJit,
            "LuaJIT preference should select LuaJIT backend when available.");
        runtime.DispatchEvent({.event_name = "runtime.luajit.test", .payload = "payload"});
        runtime.Tick({.tick_index = 3, .fixed_delta_seconds = 1.0 / 60.0});
        runtime.Shutdown();
    } else {
        passed &= Expect(
            !error.empty(),
            "LuaJIT backend failure should return a readable error.");
    }

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_script_host_runtime_tests\n";
    return 0;
}
