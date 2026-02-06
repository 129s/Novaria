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

script::ScriptBackendPreference ToScriptBackendPreference(core::ScriptBackendMode mode) {
    switch (mode) {
        case core::ScriptBackendMode::Auto:
            return script::ScriptBackendPreference::Auto;
        case core::ScriptBackendMode::Stub:
            return script::ScriptBackendPreference::Stub;
        case core::ScriptBackendMode::LuaJit:
            return script::ScriptBackendPreference::LuaJit;
    }

    return script::ScriptBackendPreference::Auto;
}

net::NetBackendPreference ToNetBackendPreference(core::NetBackendMode mode) {
    switch (mode) {
        case core::NetBackendMode::Auto:
            return net::NetBackendPreference::Auto;
        case core::NetBackendMode::Stub:
            return net::NetBackendPreference::Stub;
        case core::NetBackendMode::UdpLoopback:
            return net::NetBackendPreference::UdpLoopback;
    }

    return net::NetBackendPreference::Stub;
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

    script_host_.SetBackendPreference(ToScriptBackendPreference(config_.script_backend_mode));
    core::Logger::Info(
        "script",
        "Configured script backend preference: " +
            std::string(core::ScriptBackendModeName(config_.script_backend_mode)));
    net_service_.SetBackendPreference(ToNetBackendPreference(config_.net_backend_mode));
    net_service_.ConfigureUdpBackend(
        static_cast<std::uint16_t>(config_.net_udp_local_port),
        net::UdpEndpoint{
            .host = config_.net_udp_remote_host,
            .port = static_cast<std::uint16_t>(config_.net_udp_remote_port),
        });
    core::Logger::Info(
        "net",
        "Configured net backend preference: " +
            std::string(core::NetBackendModeName(config_.net_backend_mode)) +
            ", udp_local_port=" + std::to_string(config_.net_udp_local_port) +
            ", udp_remote=" + config_.net_udp_remote_host +
            ":" + std::to_string(config_.net_udp_remote_port));

    std::string runtime_error;
    if (!simulation_kernel_.Initialize(runtime_error)) {
        core::Logger::Error("app", "Simulation kernel initialization failed: " + runtime_error);
        sdl_context_.Shutdown();
        return false;
    }
    const script::ScriptRuntimeDescriptor script_runtime_descriptor = script_host_.RuntimeDescriptor();
    core::Logger::Info(
        "script",
        "Script runtime active: backend=" + script_runtime_descriptor.backend_name +
            ", api_version=" + script_runtime_descriptor.api_version +
            ", sandbox=" + (script_runtime_descriptor.sandbox_enabled ? "true" : "false"));

    save::WorldSaveState loaded_save_state{};
    bool has_loaded_save_state = false;

    std::string save_error;
    if (!save_repository_.Initialize(save_root_, save_error)) {
        core::Logger::Warn("save", "Save repository initialize failed: " + save_error);
    } else {
        if (save_repository_.LoadWorldState(loaded_save_state, save_error)) {
            has_loaded_save_state = true;
            local_player_id_ = loaded_save_state.local_player_id == 0 ? 1 : loaded_save_state.local_player_id;
            if (loaded_save_state.has_gameplay_snapshot) {
                simulation_kernel_.RestoreGameplayProgress(sim::GameplayProgressSnapshot{
                    .wood_collected = loaded_save_state.gameplay_wood_collected,
                    .stone_collected = loaded_save_state.gameplay_stone_collected,
                    .workbench_built = loaded_save_state.gameplay_workbench_built,
                    .sword_crafted = loaded_save_state.gameplay_sword_crafted,
                    .enemy_kill_count = loaded_save_state.gameplay_enemy_kill_count,
                    .boss_health = loaded_save_state.gameplay_boss_health,
                    .boss_defeated = loaded_save_state.gameplay_boss_defeated,
                    .playable_loop_complete = loaded_save_state.gameplay_loop_complete,
                });
            }
            core::Logger::Info(
                "save",
                "Loaded world save: version=" + std::to_string(loaded_save_state.format_version) +
                    ", tick=" + std::to_string(loaded_save_state.tick_index) +
                    ", player=" + std::to_string(local_player_id_));
            if (loaded_save_state.has_gameplay_snapshot) {
                core::Logger::Info(
                    "save",
                    "Loaded gameplay snapshot: wood=" +
                        std::to_string(loaded_save_state.gameplay_wood_collected) +
                        ", stone=" + std::to_string(loaded_save_state.gameplay_stone_collected) +
                        ", workbench=" +
                        (loaded_save_state.gameplay_workbench_built ? "true" : "false") +
                        ", sword=" +
                        (loaded_save_state.gameplay_sword_crafted ? "true" : "false") +
                        ", enemy_kills=" +
                        std::to_string(loaded_save_state.gameplay_enemy_kill_count) +
                        ", boss_health=" + std::to_string(loaded_save_state.gameplay_boss_health) +
                        ", boss_defeated=" +
                        (loaded_save_state.gameplay_boss_defeated ? "true" : "false") +
                        ", loop_complete=" +
                        (loaded_save_state.gameplay_loop_complete ? "true" : "false"));
            }
            core::Logger::Info(
                "save",
                "Loaded debug net snapshot: transitions=" +
                    std::to_string(loaded_save_state.debug_net_session_transitions) +
                    ", timeout_disconnects=" +
                    std::to_string(loaded_save_state.debug_net_timeout_disconnects) +
                    ", manual_disconnects=" +
                    std::to_string(loaded_save_state.debug_net_manual_disconnects) +
                    ", last_heartbeat_tick=" +
                    std::to_string(loaded_save_state.debug_net_last_heartbeat_tick) +
                    ", dropped_commands=" +
                    std::to_string(loaded_save_state.debug_net_dropped_commands) +
                    ", dropped_payloads=" +
                    std::to_string(loaded_save_state.debug_net_dropped_remote_payloads) +
                    ", last_transition_reason=" +
                    loaded_save_state.debug_net_last_transition_reason);
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
        std::size_t item_definition_count = 0;
        std::size_t recipe_definition_count = 0;
        std::size_t npc_definition_count = 0;
        for (const auto& manifest : loaded_mods_) {
            item_definition_count += manifest.items.size();
            recipe_definition_count += manifest.recipes.size();
            npc_definition_count += manifest.npcs.size();
        }
        core::Logger::Info("mod", "Loaded mods: " + std::to_string(loaded_mods_.size()));
        core::Logger::Info(
            "mod",
            "Loaded mod content definitions: items=" + std::to_string(item_definition_count) +
                ", recipes=" + std::to_string(recipe_definition_count) +
                ", npcs=" + std::to_string(npc_definition_count));
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

            if (frame_actions_.gameplay_collect_wood) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kGameplayCollectResource),
                    .payload = sim::command::BuildCollectResourcePayload(
                        sim::command::kResourceWood,
                        5),
                });
            }

            if (frame_actions_.gameplay_collect_stone) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kGameplayCollectResource),
                    .payload = sim::command::BuildCollectResourcePayload(
                        sim::command::kResourceStone,
                        5),
                });
            }

            if (frame_actions_.gameplay_build_workbench) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kGameplayBuildWorkbench),
                    .payload = "",
                });
            }

            if (frame_actions_.gameplay_craft_sword) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kGameplayCraftSword),
                    .payload = "",
                });
            }

            if (frame_actions_.gameplay_attack_enemy) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kGameplayAttackEnemy),
                    .payload = "",
                });
            }

            if (frame_actions_.gameplay_attack_boss) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kGameplayAttackBoss),
                    .payload = "",
                });
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
                    ", last_transition_reason=" + diagnostics.last_session_transition_reason +
                    ", last_heartbeat_tick=" + std::to_string(diagnostics.last_heartbeat_tick) +
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
            const sim::GameplayProgressSnapshot gameplay_progress = simulation_kernel_.GameplayProgress();
            core::Logger::Info(
                "sim",
                "Gameplay: wood=" + std::to_string(gameplay_progress.wood_collected) +
                    ", stone=" + std::to_string(gameplay_progress.stone_collected) +
                    ", workbench=" + (gameplay_progress.workbench_built ? "true" : "false") +
                    ", sword=" + (gameplay_progress.sword_crafted ? "true" : "false") +
                    ", enemy_kills=" + std::to_string(gameplay_progress.enemy_kill_count) +
                    ", boss_health=" + std::to_string(gameplay_progress.boss_health) +
                    ", boss_defeated=" + (gameplay_progress.boss_defeated ? "true" : "false") +
                    ", loop_complete=" +
                    (gameplay_progress.playable_loop_complete ? "true" : "false"));
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
    const sim::GameplayProgressSnapshot gameplay_progress = simulation_kernel_.GameplayProgress();
    const save::WorldSaveState save_state{
        .tick_index = simulation_kernel_.CurrentTick(),
        .local_player_id = local_player_id_,
        .mod_manifest_fingerprint = mod_manifest_fingerprint_,
        .gameplay_wood_collected = gameplay_progress.wood_collected,
        .gameplay_stone_collected = gameplay_progress.stone_collected,
        .gameplay_workbench_built = gameplay_progress.workbench_built,
        .gameplay_sword_crafted = gameplay_progress.sword_crafted,
        .gameplay_enemy_kill_count = gameplay_progress.enemy_kill_count,
        .gameplay_boss_health = gameplay_progress.boss_health,
        .gameplay_boss_defeated = gameplay_progress.boss_defeated,
        .gameplay_loop_complete = gameplay_progress.playable_loop_complete,
        .has_gameplay_snapshot = true,
        .debug_net_session_transitions = diagnostics.session_transition_count,
        .debug_net_timeout_disconnects = diagnostics.timeout_disconnect_count,
        .debug_net_manual_disconnects = diagnostics.manual_disconnect_count,
        .debug_net_last_heartbeat_tick = diagnostics.last_heartbeat_tick,
        .debug_net_dropped_commands = diagnostics.dropped_command_count,
        .debug_net_dropped_remote_payloads = diagnostics.dropped_remote_chunk_payload_count,
        .debug_net_last_transition_reason = diagnostics.last_session_transition_reason,
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
