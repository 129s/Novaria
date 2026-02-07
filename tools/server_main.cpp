#include "core/config.h"
#include "core/logger.h"
#include "net/net_service_runtime.h"
#include "script/script_host.h"
#include "sim/command_schema.h"
#include "sim/simulation_kernel.h"
#include "world/world_service_basic.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct ServerOptions final {
    std::filesystem::path config_path = "config/game.toml";
    std::uint64_t ticks = 0;
    double fixed_delta_seconds = 1.0 / 60.0;
    std::uint64_t log_interval_ticks = 300;
};

std::atomic_bool g_keep_running{true};

void OnSignal(int signal_code) {
    (void)signal_code;
    g_keep_running.store(false);
}

class ServerScriptHost final : public novaria::script::IScriptHost {
public:
    bool Initialize(std::string& out_error) override {
        out_error.clear();
        return true;
    }

    void Shutdown() override {}

    void Tick(const novaria::sim::TickContext& tick_context) override {
        (void)tick_context;
    }

    void DispatchEvent(const novaria::script::ScriptEvent& event_data) override {
        (void)event_data;
    }

    novaria::script::ScriptRuntimeDescriptor RuntimeDescriptor() const override {
        return novaria::script::ScriptRuntimeDescriptor{
            .backend_name = "server_null",
            .api_version = novaria::script::kScriptApiVersion,
            .sandbox_enabled = true,
        };
    }
};

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
        << "[--fixed-delta <seconds>] [--log-interval <ticks>]\n"
        << "\n"
        << "Examples:\n"
        << "  novaria_server --config config/game.toml --ticks 7200\n"
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
    ServerScriptHost script_host;
    novaria::sim::SimulationKernel simulation_kernel(world_service, net_service, script_host);

    net_service.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    net_service.ConfigureUdpBackend(
        config.net_udp_local_host,
        static_cast<std::uint16_t>(config.net_udp_local_port),
        novaria::net::UdpEndpoint{
            .host = config.net_udp_remote_host,
            .port = static_cast<std::uint16_t>(config.net_udp_remote_port),
        });

    if (!simulation_kernel.Initialize(error)) {
        std::cerr << "[ERROR] server initialize failed: " << error << '\n';
        return 1;
    }

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
    novaria::core::Logger::Info("server", "Server stopped.");
    return 0;
}
