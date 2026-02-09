#include "core/config.h"
#include "core/executable_path.h"
#include "core/logger.h"
#include "runtime/mod_pipeline.h"
#include "runtime/net_service_factory.h"
#include "runtime/script_host_factory.h"
#include "runtime/runtime_paths.h"
#include "runtime/world_service_factory.h"
#include "sim/command_schema.h"
#include "sim/simulation_kernel.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct ServerOptions final {
    std::filesystem::path config_path;
    std::filesystem::path mod_root;
    bool mods_overridden = false;
    std::uint64_t ticks = 0;
    double fixed_delta_seconds = 1.0 / 60.0;
    std::uint64_t log_interval_ticks = 300;
};

std::atomic_bool g_keep_running{true};

void OnSignal(int signal_code) {
    (void)signal_code;
    g_keep_running.store(false);
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
            out_options.mods_overridden = true;
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
        << "  novaria_server --config novaria_server.cfg --mods mods --ticks 7200\n"
        << "  novaria_server --config novaria_server.cfg --fixed-delta 0.0166667\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path executable_path = novaria::core::GetExecutablePath();
    const std::filesystem::path exe_dir = executable_path.parent_path();
    const std::filesystem::path default_config_path =
        exe_dir / (executable_path.stem().string() + ".cfg");

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
    if (!novaria::core::ConfigLoader::LoadEmbeddedDefaults(config, error)) {
        novaria::core::Logger::Warn("server", "Embedded default config load failed: " + error);
    }

    std::filesystem::path resolved_config_path = options.config_path;
    if (resolved_config_path.empty()) {
        resolved_config_path = default_config_path;
    } else if (resolved_config_path.is_relative()) {
        resolved_config_path = exe_dir / resolved_config_path;
    }
    resolved_config_path = resolved_config_path.lexically_normal();

    if (std::filesystem::exists(resolved_config_path)) {
        if (!novaria::core::ConfigLoader::Load(resolved_config_path, config, error)) {
            novaria::core::Logger::Warn(
                "server",
                "Config override load failed, ignoring: " + error);
        } else {
            novaria::core::Logger::Info(
                "server",
                "Config loaded: " + resolved_config_path.string());
        }
    } else {
        novaria::core::Logger::Info(
            "server",
            "Config override not found, using defaults: " + resolved_config_path.string());
    }

    std::filesystem::path mod_root = options.mod_root;
    if (!options.mods_overridden) {
        mod_root = novaria::runtime::ResolveRuntimePaths(exe_dir, config).mod_root;
    } else if (!mod_root.empty() && mod_root.is_relative()) {
        mod_root = exe_dir / mod_root;
    }
    mod_root = mod_root.lexically_normal();

    std::unique_ptr<novaria::world::IWorldService> world_service =
        novaria::runtime::CreateWorldService();
    if (!world_service) {
        std::cerr << "[ERROR] world service factory returned null\n";
        return 1;
    }
    auto net_service = novaria::runtime::CreateNetService(novaria::runtime::NetServiceConfig{
        .local_host = config.net_udp_local_host,
        .local_port = static_cast<std::uint16_t>(config.net_udp_local_port),
        .remote_endpoint = novaria::net::UdpEndpoint{
            .host = config.net_udp_remote_host,
            .port = static_cast<std::uint16_t>(config.net_udp_remote_port),
        },
    });
    auto script_host = novaria::runtime::CreateScriptHost();

    novaria::mod::ModLoader mod_loader;
    std::vector<novaria::mod::ModManifest> loaded_mods;
    std::string mod_manifest_fingerprint;
    std::vector<novaria::script::ScriptModuleSource> script_modules;
    if (!novaria::runtime::LoadModsAndScripts(
            mod_root,
            mod_loader,
            loaded_mods,
            mod_manifest_fingerprint,
            script_modules,
            error)) {
        std::cerr << "[ERROR] load mods and scripts failed: " << error << '\n';
        mod_loader.Shutdown();
        return 1;
    }

    if (!script_host->SetScriptModules(std::move(script_modules), error)) {
        std::cerr << "[ERROR] load mod script modules failed: " << error << '\n';
        mod_loader.Shutdown();
        return 1;
    }

    novaria::sim::SimulationKernel simulation_kernel(*world_service, *net_service, *script_host);
    simulation_kernel.SetAuthorityMode(novaria::sim::SimulationAuthorityMode::Authority);

    if (!simulation_kernel.Initialize(error)) {
        std::cerr << "[ERROR] server initialize failed: " << error << '\n';
        mod_loader.Shutdown();
        return 1;
    }
    const novaria::script::ScriptRuntimeDescriptor script_runtime_descriptor =
        script_host->RuntimeDescriptor();
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
                .command_id = novaria::sim::command::kWorldLoadChunk,
                .payload = novaria::sim::command::EncodeWorldChunkPayload({.chunk_x = chunk_x, .chunk_y = chunk_y}),
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
            const novaria::net::NetDiagnosticsSnapshot diagnostics =
                net_service->DiagnosticsSnapshot();
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
