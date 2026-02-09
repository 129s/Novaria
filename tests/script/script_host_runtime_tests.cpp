#include "runtime/script_host_factory.h"

#include <iostream>
#include <string>
#include <utility>

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
    auto runtime = novaria::runtime::CreateScriptHost();
    std::string error;

    passed &= Expect(
        !runtime->SetScriptModules(
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
        !runtime->SetScriptModules(
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

    passed &= Expect(
        !runtime->SetScriptModules(
            {{
                .module_name = "mod_bad_capability",
                .api_version = novaria::script::kScriptApiVersion,
                .capabilities = {"filesystem.write"},
                .source_code = "novaria = novaria or {}",
            }},
            error),
        "Unsupported script capability should be rejected.");
    passed &= Expect(
        !error.empty(),
        "Unsupported script capability should return readable error.");

    return passed;
}

}  // namespace

int main() {
    bool passed = true;

    auto runtime = novaria::runtime::CreateScriptHost();
    std::string error;
    passed &= TestRejectInvalidScriptModuleMetadata();

    passed &= Expect(
        runtime->SetScriptModules(
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

    const bool luajit_init_ok = runtime->Initialize(error);
#if defined(NOVARIA_WITH_LUAJIT)
    passed &= Expect(luajit_init_ok, "LuaJIT backend should initialize when LuaJIT is available.");
    if (luajit_init_ok) {
        const novaria::script::ScriptRuntimeDescriptor descriptor = runtime->RuntimeDescriptor();
        passed &= Expect(descriptor.backend_name == "luajit", "Runtime descriptor should expose luajit.");
        passed &= Expect(
            descriptor.api_version == novaria::script::kScriptApiVersion,
            "Runtime descriptor should expose expected API version.");
        passed &= Expect(
            descriptor.sandbox_enabled,
            "Runtime descriptor should expose enabled sandbox mode.");
        passed &= Expect(
            descriptor.sandbox_level == "resource_limited",
            "Runtime descriptor should expose resource-limited sandbox level.");
        passed &= Expect(
            descriptor.memory_budget_bytes >= 32 * 1024 * 1024,
            "Runtime descriptor should expose non-trivial memory budget.");
        passed &= Expect(
            descriptor.instruction_budget_per_call >= 100000,
            "Runtime descriptor should expose instruction budget.");
        passed &= Expect(
            descriptor.loaded_module_count == 1,
            "Runtime descriptor should expose loaded module count.");
        passed &= Expect(
            descriptor.active_event_handler_count == 1,
            "Runtime descriptor should expose active event handler count.");
        runtime->DispatchEvent({.event_name = "runtime.luajit.test", .payload = "payload"});
        runtime->Tick({.tick_index = 3, .fixed_delta_seconds = 1.0 / 60.0});
        runtime->Shutdown();
    }

    auto callback_bus_runtime = novaria::runtime::CreateScriptHost();
    passed &= Expect(
        callback_bus_runtime->SetScriptModules(
            {{
                .module_name = "mod_callback_a",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code =
                    "function novaria_on_tick(tick_index, delta_seconds)\n"
                    "  return tick_index + delta_seconds\n"
                    "end\n"
                    "function novaria_on_event(event_name, payload)\n"
                    "  novaria = novaria or {}\n"
                    "  novaria.callback_a = event_name .. payload\n"
                    "end\n",
            },
             {
                 .module_name = "mod_callback_b",
                 .api_version = novaria::script::kScriptApiVersion,
                 .source_code =
                     "function novaria_on_tick(tick_index, delta_seconds)\n"
                     "  return tick_index - delta_seconds\n"
                     "end\n"
                     "function novaria_on_event(event_name, payload)\n"
                     "  novaria = novaria or {}\n"
                     "  novaria.callback_b = payload .. event_name\n"
                     "end\n",
             }},
            error),
        "Multi-module callback runtime should accept staged modules.");
    const bool callback_bus_init_ok = callback_bus_runtime->Initialize(error);
    passed &= Expect(
        callback_bus_init_ok,
        "Multi-module callback runtime should initialize.");
    if (callback_bus_init_ok) {
        const novaria::script::ScriptRuntimeDescriptor callback_descriptor =
            callback_bus_runtime->RuntimeDescriptor();
        passed &= Expect(
            callback_descriptor.loaded_module_count == 2,
            "Multi-module runtime should report two loaded modules.");
        passed &= Expect(
            callback_descriptor.active_tick_handler_count == 2,
            "Multi-module runtime should keep both tick handlers active.");
        passed &= Expect(
            callback_descriptor.active_event_handler_count == 2,
            "Multi-module runtime should keep both event handlers active.");
        callback_bus_runtime->DispatchEvent({.event_name = "event", .payload = "payload"});
        callback_bus_runtime->Tick({.tick_index = 5, .fixed_delta_seconds = 1.0 / 60.0});
    }
    callback_bus_runtime->Shutdown();

    auto bad_module_runtime = novaria::runtime::CreateScriptHost();
    passed &= Expect(
        bad_module_runtime->SetScriptModules(
            {{
                .module_name = "mod_broken_script",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code = "function novaria_on_event(event_name, payload) syntax_error end",
            }},
            error),
        "Broken script syntax should be accepted during staging.");
    passed &= Expect(
        !bad_module_runtime->Initialize(error),
        "Broken script syntax should fail runtime initialization.");
    passed &= Expect(
        !error.empty(),
        "Broken script syntax failure should return readable error.");

    auto instruction_budget_runtime = novaria::runtime::CreateScriptHost();
    passed &= Expect(
        instruction_budget_runtime->SetScriptModules(
            {{
                .module_name = "mod_instruction_budget_pressure",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code =
                    "local sum = 0\n"
                    "for i = 1, 5000000 do\n"
                    "  sum = sum + i\n"
                    "end\n"
                    "novaria = novaria or {}\n"
                    "novaria.sum = sum\n",
            }},
            error),
        "Instruction-budget pressure script should be accepted during staging.");
    const bool instruction_budget_init_ok = instruction_budget_runtime->Initialize(error);
    if (instruction_budget_init_ok) {
        instruction_budget_runtime->Shutdown();
    } else {
        passed &= Expect(
            error.find("instruction budget exceeded") != std::string::npos,
            "Instruction-budget failure should include budget exceeded reason.");
    }

    auto isolated_modules_runtime = novaria::runtime::CreateScriptHost();
    passed &= Expect(
        isolated_modules_runtime->SetScriptModules(
            {{
                .module_name = "mod_isolated_a",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code = "sandbox_internal = 42",
            },
             {
                 .module_name = "mod_isolated_b",
                 .api_version = novaria::script::kScriptApiVersion,
                 .source_code =
                     "if sandbox_internal ~= nil then error(\"module leaked global state\") end",
             }},
            error),
        "Isolated modules should pass metadata staging.");
    const bool isolated_runtime_init_ok = isolated_modules_runtime->Initialize(error);
    passed &= Expect(
        isolated_runtime_init_ok,
        "Module environments should isolate transient globals.");
    isolated_modules_runtime->Shutdown();

    auto whitelist_runtime = novaria::runtime::CreateScriptHost();
    passed &= Expect(
        whitelist_runtime->SetScriptModules(
            { {
                .module_name = "mod_whitelist_guard",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code =
                    "if io ~= nil then error(\"io should be blocked\") end\n"
                    "if os ~= nil then error(\"os should be blocked\") end\n"
                    "if package ~= nil then error(\"package should be blocked\") end\n"
                    "if require ~= nil then error(\"require should be blocked\") end\n"
                    "if _G.io ~= nil then error(\"_G.io should be blocked\") end\n"
                    "if type(math.max) ~= \"function\" then error(\"math should be allowed\") end\n"
                    "if type(string.sub) ~= \"function\" then error(\"string should be allowed\") end\n",
            } },
            error),
        "Whitelist sandbox module should pass metadata staging.");
    const bool whitelist_runtime_init_ok = whitelist_runtime->Initialize(error);
    passed &= Expect(
        whitelist_runtime_init_ok,
        "Whitelist sandbox should block dangerous globals and keep safe stdlib.");
    whitelist_runtime->Shutdown();

    auto memory_pressure_runtime = novaria::runtime::CreateScriptHost();
    passed &= Expect(
        memory_pressure_runtime->SetScriptModules(
            {{
                .module_name = "mod_memory_pressure",
                .api_version = novaria::script::kScriptApiVersion,
                .source_code =
                    "local huge_blob = string.rep(\"x\", 80 * 1024 * 1024)\n"
                    "novaria = novaria or {}\n"
                    "novaria.huge_blob = huge_blob\n",
            }},
            error),
        "Memory pressure module should pass metadata staging.");
    passed &= Expect(
        !memory_pressure_runtime->Initialize(error),
        "Memory pressure module should fail within sandbox budget.");
    passed &= Expect(
        error.find("memory") != std::string::npos ||
            error.find("not enough") != std::string::npos,
        "Memory pressure failure should expose memory-related reason.");
    memory_pressure_runtime->Shutdown();
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
