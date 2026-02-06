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

bool TestRejectInvalidScriptModuleMetadata() {
    bool passed = true;
    novaria::script::ScriptHostRuntime runtime;
    std::string error;

    passed &= Expect(
        !runtime.SetScriptModules(
            {{
                .module_name = "mod_bad_api",
                .api_version = "9.9.9",
                .source_code = "novaria = novaria or {}",
            }},
            error),
        "Mismatched script API version should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Mismatched script API version should return readable error.");

    passed &= Expect(
        !runtime.SetScriptModules(
            {{
                .module_name = "duplicated_mod",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code = "novaria = novaria or {}",
            },
             {
                 .module_name = "duplicated_mod",
                 .api_version = novaria::script::kScriptApiVersion,
                 .source_code = "novaria = novaria or {}",
             }},
            error),
        "Duplicate script module names should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Duplicate script module names should return readable error.");

    return passed;
}

}  // namespace

int main() {
    bool passed = true;

    novaria::script::ScriptHostRuntime runtime;
    std::string error;
    passed &= TestRejectInvalidScriptModuleMetadata();

    passed &= Expect(
        runtime.SetScriptModules(
            {{
                .module_name = "mod_content_core",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code =
                    "novaria = novaria or {}\n"
                    "novaria.module_loaded = true\n"
                    "function novaria_on_event(event_name, payload)\n"
                    "  novaria.last_event_name = \"module:\" .. event_name\n"
                    "  novaria.last_event_payload = payload\n"
                    "end\n",
            }},
            error),
        "Runtime should accept valid staged script modules.");
    passed &= Expect(error.empty(), "Staged module setup should not return error.");

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
        passed &= Expect(
            descriptor.sandbox_enabled,
            "Runtime descriptor should expose enabled sandbox mode.");
        runtime.DispatchEvent({.event_name = "runtime.luajit.test", .payload = "payload"});
        runtime.Tick({.tick_index = 3, .fixed_delta_seconds = 1.0 / 60.0});
        runtime.Shutdown();
    }

    novaria::script::ScriptHostRuntime bad_module_runtime;
    passed &= Expect(
        bad_module_runtime.SetScriptModules(
            {{
                .module_name = "mod_broken_script",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code = "function novaria_on_event(event_name, payload) syntax_error end",
            }},
            error),
        "Broken script syntax should be accepted during staging.");
    passed &= Expect(
        !bad_module_runtime.Initialize(error),
        "Broken script syntax should fail runtime initialization.");
    passed &= Expect(
        !error.empty(),
        "Broken script syntax failure should return readable error.");
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
