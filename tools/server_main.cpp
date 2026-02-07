#include "core/config.h"
#include "core/logger.h"
#include "mod/mod_loader.h"
#include "net/net_service_runtime.h"
#include "script/script_host_runtime.h"
#include "sim/command_schema.h"
#include "sim/simulation_kernel.h"
#include "world/world_service_basic.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct ServerOptions final {
    std::filesystem::path config_path = "config/game.toml";
    std::filesystem::path mod_root = "mods";
    std::uint64_t ticks = 0;
    double fixed_delta_seconds = 1.0 / 60.0;
    std::uint64_t log_interval_ticks = 300;
};

std::atomic_bool g_keep_running{true};

void OnSignal(int signal_code) {
    (void)signal_code;
    g_keep_running.store(false);
}

novaria::script::ScriptBackendPreference ToScriptBackendPreference(
    novaria::core::ScriptBackendMode mode) {
    switch (mode) {
        case novaria::core::ScriptBackendMode::LuaJit:
            return novaria::script::ScriptBackendPreference::LuaJit;
    }

    return novaria::script::ScriptBackendPreference::LuaJit;
}

bool ReadTextFile(
    const std::filesystem::path& file_path,
    std::string& out_text,
    std::string& out_error) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        out_error = "cannot open file: " + file_path.string();
        return false;
    }

    out_text.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    out_error.clear();
    return true;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }

    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }

    return true;
}

bool BuildModScriptModules(
    const std::vector<novaria::mod::ModManifest>& manifests,
    std::vector<novaria::script::ScriptModuleSource>& out_modules,
    std::string& out_error) {
    out_modules.clear();
    for (const auto& manifest : manifests) {
        if (manifest.script_entry.empty()) {
            continue;
        }

        const std::filesystem::path script_entry_path =
            std::filesystem::path(manifest.script_entry).lexically_normal();
        if (!IsSafeRelativePath(script_entry_path)) {
            out_error =
                "Invalid script entry path in mod '" + manifest.name +
                "': " + manifest.script_entry;
            return false;
        }

        const std::filesystem::path module_file_path =
            (manifest.root_path / script_entry_path).lexically_normal();
        std::string module_source;
        if (!ReadTextFile(module_file_path, module_source, out_error)) {
            out_error =
                "Failed to load script entry for mod '" + manifest.name +
                "': " + out_error;
            return false;
        }

        out_modules.push_back(novaria::script::ScriptModuleSource{
            .module_name = manifest.name,
            .api_version =
                manifest.script_api_version.empty()
                    ? std::string(novaria::script::kScriptApiVersion)
                    : manifest.script_api_version,
            .capabilities =
                manifest.script_capabilities.empty()
                    ? std::vector<std::string>{"event.receive", "tick.receive"}
                    : manifest.script_capabilities,
            .source_code = std::move(module_source),
        });
    }

    out_error.clear();
    return true;
}

bool ParseUInt64(std::string_view text, std::uint64_t& out_value) {
    try {
        out_value = static_cast<std::uint64_t>(std::stoull(std::string(text)));
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseDouble(std::string_view text, double& out_value) {
    try {
        out_value = std::stod(std::string(text));
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseArguments(
    int argc,
    char** argv,
    ServerOptions& out_options,
    std::string& out_error) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto read_value = [&](const char* key) -> std::string {
            if (index + 1 >= argc) {
                out_error = std::string("Missing value for option: ") + key;
                return {};
            }
            ++index;
            return argv[index];
        };

        if (arg == "--config") {
            const std::string value = read_value("--config");
            if (value.empty()) {
                return false;
            }
            out_options.config_path = value;
            continue;
        }

        if (arg == "--ticks") {
            const std::string value = read_value("--ticks");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt64(value, out_options.ticks)) {
                out_error = "Invalid --ticks value";
                return false;
            }
            continue;
        }

        if (arg == "--mods") {
            const std::string value = read_value("--mods");
            if (value.empty()) {
                return false;
            }
            out_options.mod_root = value;
            continue;
        }

        if (arg == "--fixed-delta") {
            const std::string value = read_value("--fixed-delta");
            if (value.empty()) {
                return false;
            }
            if (!ParseDouble(value, out_options.fixed_delta_seconds)) {
                out_error = "Invalid --fixed-delta value";
                return false;
            }
            continue;
        }

        if (arg == "--log-interval") {
            const std::string value = read_value("--log-interval");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt64(value, out_options.log_interval_ticks)) {
                out_error = "Invalid --log-interval value";
                return false;
            }
            continue;
        }

        out_error = "Unknown option: " + arg;
        return false;
    }

    if (out_options.fixed_delta_seconds <= 0.0) {
        out_error = "--fixed-delta must be > 0";
        return false;
    }

    out_error.clear();
    return true;
}

void PrintUsage() {
    std::cout
        << "Usage:\n"
        << "  novaria_server [--config <path>] [--ticks <count>] "
        << "[--mods <path>] [--fixed-delta <seconds>] [--log-interval <ticks>]\n"
        << "\n"
        << "Examples:\n"
        << "  novaria_server --config config/game.toml --mods mods --ticks 7200\n"
        << "  novaria_server --config config/game.toml --fixed-delta 0.0166667\n";
}

}  // namespace

int main(int argc, char** argv) {
    ServerOptions options{};
    std::string error;
    if (!ParseArguments(argc, argv, options, error)) {
        std::cerr << "[ERROR] " << error << '\n';
        PrintUsage();
        return 1;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    novaria::core::GameConfig config{};
    if (!novaria::core::ConfigLoader::Load(options.config_path, config, error)) {
        novaria::core::Logger::Warn(
            "server",
            "Config load failed, using defaults: " + error);
    } else {
        novaria::core::Logger::Info(
            "server",
            "Config loaded: " + options.config_path.string());
    }

    novaria::world::WorldServiceBasic world_service;
    novaria::net::NetServiceRuntime net_service;
    novaria::script::ScriptHostRuntime script_host;
    novaria::sim::SimulationKernel simulation_kernel(world_service, net_service, script_host);
    simulation_kernel.SetAuthorityMode(novaria::sim::SimulationAuthorityMode::Authority);

    net_service.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    net_service.ConfigureUdpBackend(
        config.net_udp_local_host,
        static_cast<std::uint16_t>(config.net_udp_local_port),
        novaria::net::UdpEndpoint{
            .host = config.net_udp_remote_host,
            .port = static_cast<std::uint16_t>(config.net_udp_remote_port),
        });
    script_host.SetBackendPreference(ToScriptBackendPreference(config.script_backend_mode));

    novaria::mod::ModLoader mod_loader;
    std::vector<novaria::mod::ModManifest> loaded_mods;
    std::string mod_error;
    if (!mod_loader.Initialize(options.mod_root, mod_error)) {
        novaria::core::Logger::Warn("mod", "Mod loader initialize failed: " + mod_error);
    } else if (!mod_loader.LoadAll(loaded_mods, mod_error)) {
        novaria::core::Logger::Warn("mod", "Mod loading failed: " + mod_error);
    } else {
        novaria::core::Logger::Info("mod", "Loaded mods: " + std::to_string(loaded_mods.size()));
        novaria::core::Logger::Info(
            "mod",
            "Manifest fingerprint: " + novaria::mod::ModLoader::BuildManifestFingerprint(loaded_mods));
    }

    std::vector<novaria::script::ScriptModuleSource> script_modules;
    if (!BuildModScriptModules(loaded_mods, script_modules, error)) {
        std::cerr << "[ERROR] build mod script modules failed: " << error << '\n';
        mod_loader.Shutdown();
        return 1;
    }
    if (!script_host.SetScriptModules(std::move(script_modules), error)) {
        std::cerr << "[ERROR] load mod script modules failed: " << error << '\n';
        mod_loader.Shutdown();
        return 1;
    }

    if (!simulation_kernel.Initialize(error)) {
        std::cerr << "[ERROR] server initialize failed: " << error << '\n';
        mod_loader.Shutdown();
        return 1;
    }
    const novaria::script::ScriptRuntimeDescriptor script_runtime_descriptor = script_host.RuntimeDescriptor();
    novaria::core::Logger::Info(
        "script",
        "Script runtime active: backend=" + script_runtime_descriptor.backend_name +
            ", api_version=" + script_runtime_descriptor.api_version +
            ", sandbox=" + (script_runtime_descriptor.sandbox_enabled ? "true" : "false"));

    const int preload_chunk_radius = 2;
    for (int chunk_y = -preload_chunk_radius; chunk_y <= preload_chunk_radius; ++chunk_y) {
        for (int chunk_x = -preload_chunk_radius; chunk_x <= preload_chunk_radius; ++chunk_x) {
            simulation_kernel.SubmitLocalCommand(novaria::net::PlayerCommand{
                .player_id = 1,
                .command_type = std::string(novaria::sim::command::kWorldLoadChunk),
                .payload = novaria::sim::command::BuildWorldChunkPayload(
                    chunk_x,
                    chunk_y),
            });
        }
    }
    simulation_kernel.Update(options.fixed_delta_seconds);

    novaria::core::Logger::Info(
        "server",
        "Server started: local=" + config.net_udp_local_host +
            ":" + std::to_string(config.net_udp_local_port) +
            ", remote=" + config.net_udp_remote_host +
            ":" + std::to_string(config.net_udp_remote_port) +
            ", ticks_limit=" + std::to_string(options.ticks));

    while (g_keep_running.load()) {
        const std::uint64_t current_tick = simulation_kernel.CurrentTick();
        if (options.ticks > 0 && current_tick >= options.ticks) {
            break;
        }

        simulation_kernel.Update(options.fixed_delta_seconds);

        if (options.log_interval_ticks > 0 &&
            current_tick > 0 &&
            current_tick % options.log_interval_ticks == 0) {
            const novaria::net::NetDiagnosticsSnapshot diagnostics = net_service.DiagnosticsSnapshot();
            novaria::core::Logger::Info(
                "server",
                "Tick=" + std::to_string(current_tick) +
                    ", session_state=" + std::to_string(static_cast<int>(diagnostics.session_state)) +
                    ", transitions=" + std::to_string(diagnostics.session_transition_count) +
                    ", timeout_disconnects=" + std::to_string(diagnostics.timeout_disconnect_count) +
                    ", ignored_senders=" + std::to_string(diagnostics.ignored_unexpected_sender_count));
        }

        const auto sleep_duration = std::chrono::duration<double>(options.fixed_delta_seconds);
        std::this_thread::sleep_for(sleep_duration);
    }

    simulation_kernel.Shutdown();
    mod_loader.Shutdown();
    novaria::core::Logger::Info("server", "Server stopped.");
    return 0;
}
