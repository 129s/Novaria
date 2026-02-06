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

    runtime.SetBackendPreference(novaria::script::ScriptBackendPreference::LuaJit);
    const bool luajit_init_ok = runtime.Initialize(error);
#if defined(NOVARIA_WITH_LUAJIT)
    passed &= Expect(luajit_init_ok, "LuaJIT backend should initialize when LuaJIT is available.");
    if (luajit_init_ok) {
        passed &= Expect(
            runtime.ActiveBackend() == novaria::script::ScriptBackendKind::LuaJit,
            "LuaJIT preference should select LuaJIT backend.");
        const novaria::script::ScriptRuntimeDescriptor descriptor = runtime.RuntimeDescriptor();
        passed &= Expect(descriptor.backend_name == "luajit", "Runtime descriptor should expose luajit.");
        passed &= Expect(
            descriptor.api_version == novaria::script::kScriptApiVersion,
            "Runtime descriptor should expose expected API version.");
        runtime.DispatchEvent({.event_name = "runtime.luajit.test", .payload = "payload"});
        runtime.Tick({.tick_index = 3, .fixed_delta_seconds = 1.0 / 60.0});
        runtime.Shutdown();
    }
#else
    passed &= Expect(!luajit_init_ok, "LuaJIT backend should fail-fast when LuaJIT is unavailable.");
    passed &= Expect(
        !error.empty(),
        "LuaJIT backend failure should return a readable error.");
#endif

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_script_host_runtime_tests\n";
    return 0;
}
