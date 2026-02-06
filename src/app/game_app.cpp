#include "app/game_app.h"

#include "app/game_loop.h"
#include "core/logger.h"
#include "sim/command_schema.h"

#include <string>

namespace novaria::app {
namespace {

const char* NetSessionStateName(net::NetSessionState state) {
    switch (state) {
        case net::NetSessionState::Disconnected:
            return "disconnected";
        case net::NetSessionState::Connecting:
            return "connecting";
        case net::NetSessionState::Connected:
            return "connected";
    }

    return "unknown";
}

}  // namespace

GameApp::GameApp()
    : simulation_kernel_(world_service_, net_service_, script_host_) {}

bool GameApp::Initialize(const std::filesystem::path& config_path) {
    std::string config_error;
    if (!core::ConfigLoader::Load(config_path, config_, config_error)) {
        core::Logger::Warn("config", "Config load failed, using defaults: " + config_error);
    } else {
        core::Logger::Info("config", "Config loaded: " + config_path.string());
    }

    if (!sdl_context_.Initialize(config_)) {
        core::Logger::Error("app", "SDL3 initialization failed.");
        return false;
    }

    std::string runtime_error;
    if (!simulation_kernel_.Initialize(runtime_error)) {
        core::Logger::Error("app", "Simulation kernel initialization failed: " + runtime_error);
        sdl_context_.Shutdown();
        return false;
    }

    save::WorldSaveState loaded_save_state{};
    bool has_loaded_save_state = false;

    std::string save_error;
    if (!save_repository_.Initialize(save_root_, save_error)) {
        core::Logger::Warn("save", "Save repository initialize failed: " + save_error);
    } else {
        if (save_repository_.LoadWorldState(loaded_save_state, save_error)) {
            has_loaded_save_state = true;
            local_player_id_ = loaded_save_state.local_player_id == 0 ? 1 : loaded_save_state.local_player_id;
            core::Logger::Info(
                "save",
                "Loaded world save: version=" + std::to_string(loaded_save_state.format_version) +
                    ", tick=" + std::to_string(loaded_save_state.tick_index) +
                    ", player=" + std::to_string(local_player_id_));
            core::Logger::Info(
                "save",
                "Loaded debug net snapshot: transitions=" +
                    std::to_string(loaded_save_state.debug_net_session_transitions) +
                    ", timeout_disconnects=" +
                    std::to_string(loaded_save_state.debug_net_timeout_disconnects) +
                    ", manual_disconnects=" +
                    std::to_string(loaded_save_state.debug_net_manual_disconnects) +
                    ", dropped_commands=" +
                    std::to_string(loaded_save_state.debug_net_dropped_commands) +
                    ", dropped_payloads=" +
                    std::to_string(loaded_save_state.debug_net_dropped_remote_payloads));
        } else {
            core::Logger::Warn("save", "World save load skipped: " + save_error);
        }
    }

    std::string mod_error;
    mod_manifest_fingerprint_.clear();
    loaded_mods_.clear();
    if (!mod_loader_.Initialize(mod_root_, mod_error)) {
        core::Logger::Warn("mod", "Mod loader initialize failed: " + mod_error);
    } else if (!mod_loader_.LoadAll(loaded_mods_, mod_error)) {
        core::Logger::Warn("mod", "Mod loading failed: " + mod_error);
    } else {
        mod_manifest_fingerprint_ = mod::ModLoader::BuildManifestFingerprint(loaded_mods_);
        core::Logger::Info("mod", "Loaded mods: " + std::to_string(loaded_mods_.size()));
        core::Logger::Info("mod", "Manifest fingerprint: " + mod_manifest_fingerprint_);
    }

    if (has_loaded_save_state && !loaded_save_state.mod_manifest_fingerprint.empty() &&
        !mod_manifest_fingerprint_.empty() &&
        loaded_save_state.mod_manifest_fingerprint != mod_manifest_fingerprint_) {
        const std::string mismatch_message =
            "Loaded save mod fingerprint mismatch. save=" +
            loaded_save_state.mod_manifest_fingerprint +
            ", runtime=" + mod_manifest_fingerprint_;
        if (config_.strict_save_mod_fingerprint) {
            core::Logger::Error("save", mismatch_message);
            mod_manifest_fingerprint_.clear();
            loaded_mods_.clear();
            mod_loader_.Shutdown();
            save_repository_.Shutdown();
            simulation_kernel_.Shutdown();
            sdl_context_.Shutdown();
            return false;
        }
        core::Logger::Warn("save", mismatch_message);
    }

    initialized_ = true;
    last_net_diagnostics_tick_ = 0;
    core::Logger::Info("app", "Novaria started.");
    return true;
}

int GameApp::Run() {
    if (!initialized_) {
        core::Logger::Error("app", "Run called before initialization.");
        return 1;
    }

    GameLoop loop;
    loop.Run(
        [this]() -> bool {
            frame_actions_ = {};
            if (!sdl_context_.PumpEvents(quit_requested_, frame_actions_)) {
                core::Logger::Error("platform", "Event pump failed.");
                return false;
            }

            if (frame_actions_.send_jump_command) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kJump),
                    .payload = "",
                });
            }

            if (frame_actions_.send_attack_command) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kAttack),
                    .payload = "light",
                });
            }

            if (frame_actions_.emit_script_ping) {
                script_host_.DispatchEvent(script::ScriptEvent{
                    .event_name = "debug.ping",
                    .payload = std::to_string(script_ping_counter_++),
                });
            }

            if (frame_actions_.debug_set_tile_air) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kWorldSetTile),
                    .payload = sim::command::BuildWorldSetTilePayload(0, 0, 0),
                });
            }

            if (frame_actions_.debug_set_tile_stone) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kWorldSetTile),
                    .payload = sim::command::BuildWorldSetTilePayload(0, 0, 2),
                });
            }

            if (frame_actions_.debug_net_disconnect) {
                net_service_.RequestDisconnect();
                core::Logger::Warn("net", "Debug action: request disconnect.");
            }

            if (frame_actions_.debug_net_connect) {
                net_service_.RequestConnect();
                core::Logger::Info("net", "Debug action: request connect.");
            }

            if (frame_actions_.debug_net_heartbeat) {
                net_service_.NotifyHeartbeatReceived(simulation_kernel_.CurrentTick());
                core::Logger::Info(
                    "net",
                    "Debug action: heartbeat received at tick " +
                        std::to_string(simulation_kernel_.CurrentTick()) + ".");
            }

            return !quit_requested_;
        },
        [this](double fixed_delta_seconds) {
            simulation_kernel_.Update(fixed_delta_seconds);

            constexpr std::uint64_t kNetDiagnosticsPeriodTicks = 300;
            const std::uint64_t current_tick = simulation_kernel_.CurrentTick();
            if (current_tick == 0 ||
                current_tick % kNetDiagnosticsPeriodTicks != 0 ||
                current_tick == last_net_diagnostics_tick_) {
                return;
            }

            last_net_diagnostics_tick_ = current_tick;
            const net::NetDiagnosticsSnapshot diagnostics = net_service_.DiagnosticsSnapshot();
            core::Logger::Info(
                "net",
                "Diagnostics: tick=" + std::to_string(current_tick) +
                    ", state=" + NetSessionStateName(diagnostics.session_state) +
                    ", transitions=" + std::to_string(diagnostics.session_transition_count) +
                    ", connected_transitions=" + std::to_string(diagnostics.connected_transition_count) +
                    ", connect_requests=" + std::to_string(diagnostics.connect_request_count) +
                    ", timeout_disconnects=" + std::to_string(diagnostics.timeout_disconnect_count) +
                    ", manual_disconnects=" + std::to_string(diagnostics.manual_disconnect_count) +
                    ", dropped_commands(total/disconnected/queue_full)=" +
                    std::to_string(diagnostics.dropped_command_count) + "/" +
                    std::to_string(diagnostics.dropped_command_disconnected_count) + "/" +
                    std::to_string(diagnostics.dropped_command_queue_full_count) +
                    ", dropped_payloads(total/disconnected/queue_full)=" +
                    std::to_string(diagnostics.dropped_remote_chunk_payload_count) + "/" +
                    std::to_string(diagnostics.dropped_remote_chunk_payload_disconnected_count) + "/" +
                    std::to_string(diagnostics.dropped_remote_chunk_payload_queue_full_count) +
                    ", ignored_heartbeats=" + std::to_string(diagnostics.ignored_heartbeat_count));
        },
        [this](float interpolation_alpha) { sdl_context_.RenderFrame(interpolation_alpha); });

    core::Logger::Info("app", "Main loop exited.");
    return 0;
}

void GameApp::Shutdown() {
    if (!initialized_) {
        return;
    }

    std::string save_error;
    const net::NetDiagnosticsSnapshot diagnostics = net_service_.DiagnosticsSnapshot();
    const save::WorldSaveState save_state{
        .tick_index = simulation_kernel_.CurrentTick(),
        .local_player_id = local_player_id_,
        .mod_manifest_fingerprint = mod_manifest_fingerprint_,
        .debug_net_session_transitions = diagnostics.session_transition_count,
        .debug_net_timeout_disconnects = diagnostics.timeout_disconnect_count,
        .debug_net_manual_disconnects = diagnostics.manual_disconnect_count,
        .debug_net_dropped_commands = diagnostics.dropped_command_count,
        .debug_net_dropped_remote_payloads = diagnostics.dropped_remote_chunk_payload_count,
    };
    if (!save_repository_.SaveWorldState(save_state, save_error)) {
        core::Logger::Warn("save", "World save write failed: " + save_error);
    }

    mod_manifest_fingerprint_.clear();
    loaded_mods_.clear();
    mod_loader_.Shutdown();
    save_repository_.Shutdown();
    simulation_kernel_.Shutdown();
    sdl_context_.Shutdown();
    initialized_ = false;
    last_net_diagnostics_tick_ = 0;
    core::Logger::Info("app", "Novaria shutdown complete.");
}

}  // namespace novaria::app
