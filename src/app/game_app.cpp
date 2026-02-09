#include "app/game_app.h"

#include "app/game_loop.h"
#include "core/logger.h"
#include "core/executable_path.h"
#include "runtime/mod_fingerprint_policy.h"
#include "runtime/mod_pipeline.h"
#include "runtime/net_service_factory.h"
#include "runtime/runtime_paths.h"
#include "runtime/save_state_loader.h"
#include "runtime/script_host_factory.h"
#include "runtime/world_service_factory.h"
#include "world/snapshot_codec.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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

float ComputeDaylightFactor(std::uint64_t tick_index) {
    constexpr double kTickDeltaSeconds = 1.0 / 60.0;
    constexpr double kDayNightCycleSeconds = 180.0;
    constexpr double kTau = 6.28318530717958647692;
    const double elapsed_seconds = static_cast<double>(tick_index) * kTickDeltaSeconds;
    const double cycle_phase =
        std::fmod(elapsed_seconds, kDayNightCycleSeconds) / kDayNightCycleSeconds;
    const double wave = std::sin(cycle_phase * kTau);
    const double normalized = (wave + 1.0) * 0.5;
    return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

}  // namespace

GameApp::GameApp() = default;

bool GameApp::Initialize(const std::filesystem::path& config_path) {
    const std::filesystem::path executable_path = core::GetExecutablePath();
    const std::filesystem::path exe_dir = executable_path.parent_path();
    const std::filesystem::path default_config_path =
        exe_dir / (executable_path.stem().string() + ".cfg");

    std::string config_error;
    if (!core::ConfigLoader::LoadEmbeddedDefaults(config_, config_error)) {
        core::Logger::Warn("config", "Embedded default config load failed: " + config_error);
    }

    std::filesystem::path resolved_config_path = config_path;
    if (resolved_config_path.empty()) {
        resolved_config_path = default_config_path;
    } else if (resolved_config_path.is_relative()) {
        resolved_config_path = exe_dir / resolved_config_path;
    }
    resolved_config_path = resolved_config_path.lexically_normal();

    if (std::filesystem::exists(resolved_config_path)) {
        if (!core::ConfigLoader::Load(resolved_config_path, config_, config_error)) {
            core::Logger::Warn("config", "Config override load failed, ignoring: " + config_error);
        } else {
            core::Logger::Info("config", "Config loaded: " + resolved_config_path.string());
        }
    } else {
        core::Logger::Info(
            "config",
            "Config override not found, using defaults: " + resolved_config_path.string());
    }

    const runtime::RuntimePaths runtime_paths = runtime::ResolveRuntimePaths(exe_dir, config_);
    save_root_ = runtime_paths.save_root;
    mod_root_ = runtime_paths.mod_root;
    core::Logger::Info("save", "Resolved save_root: " + save_root_.string());
    core::Logger::Info("mod", "Resolved mod_root: " + mod_root_.string());

    if (!sdl_context_.Initialize(config_)) {
        core::Logger::Error("app", "SDL3 initialization failed.");
        return false;
    }

    core::Logger::Info(
        "net",
        "Configured net backend: udp_peer" +
            std::string(", udp_local=") + config_.net_udp_local_host +
            ":" + std::to_string(config_.net_udp_local_port) +
            ", udp_remote=" + config_.net_udp_remote_host +
            ":" + std::to_string(config_.net_udp_remote_port));

    save::WorldSaveState loaded_save_state{};
    bool has_loaded_save_state = false;

    std::string save_error;
    if (!save_repository_.Initialize(save_root_, save_error)) {
        core::Logger::Warn("save", "Save repository initialize failed: " + save_error);
    } else {
        runtime::SaveLoadResult save_result{};
        if (runtime::TryLoadSaveState(
                save_repository_,
                save_result,
                save_error)) {
            has_loaded_save_state = save_result.has_state;
            loaded_save_state = save_result.state;
            local_player_id_ = save_result.local_player_id;
        } else if (!save_error.empty()) {
            core::Logger::Warn("save", "World save load skipped: " + save_error);
        }
    }

    gameplay_fingerprint_.clear();
    loaded_mods_.clear();
    std::vector<script::ScriptModuleSource> script_modules;
    std::string runtime_error;
    if (!runtime::LoadModsAndScripts(
            mod_root_,
            mod_loader_,
            loaded_mods_,
            gameplay_fingerprint_,
            script_modules,
            runtime_error)) {
        core::Logger::Error("script", "Build mod script modules failed: " + runtime_error);
        gameplay_fingerprint_.clear();
        loaded_mods_.clear();
        mod_loader_.Shutdown();
        save_repository_.Shutdown();
        sdl_context_.Shutdown();
        return false;
    }

    if (has_loaded_save_state) {
        const runtime::ModFingerprintCheck fingerprint_check =
            runtime::EvaluateModFingerprint(
                loaded_save_state.gameplay_fingerprint,
                gameplay_fingerprint_,
                config_.strict_save_mod_fingerprint);
        if (fingerprint_check.decision == runtime::ModFingerprintDecision::Reject) {
            core::Logger::Error("save", fingerprint_check.message);
            gameplay_fingerprint_.clear();
            loaded_mods_.clear();
            mod_loader_.Shutdown();
            save_repository_.Shutdown();
            sdl_context_.Shutdown();
            return false;
        }
        if (fingerprint_check.decision == runtime::ModFingerprintDecision::Warn) {
            core::Logger::Warn("save", fingerprint_check.message);
        }
    }

    world_service_ = runtime::CreateWorldService();
    net_service_ = runtime::CreateNetService(runtime::NetServiceConfig{
        .local_host = config_.net_udp_local_host,
        .local_port = static_cast<std::uint16_t>(config_.net_udp_local_port),
        .remote_endpoint = net::UdpEndpoint{
            .host = config_.net_udp_remote_host,
            .port = static_cast<std::uint16_t>(config_.net_udp_remote_port),
        },
    });
    script_host_ = runtime::CreateScriptHost();

    if (!script_host_->SetScriptModules(std::move(script_modules), runtime_error)) {
        core::Logger::Error("script", "Load mod script modules failed: " + runtime_error);
        gameplay_fingerprint_.clear();
        loaded_mods_.clear();
        mod_loader_.Shutdown();
        save_repository_.Shutdown();
        sdl_context_.Shutdown();
        net_service_.reset();
        script_host_.reset();
        world_service_.reset();
        return false;
    }

    simulation_kernel_ = std::make_unique<sim::SimulationKernel>(
        *world_service_,
        *net_service_,
        *script_host_);
    simulation_kernel_->SetLocalPlayerId(local_player_id_);

    if (!simulation_kernel_->Initialize(runtime_error)) {
        core::Logger::Error("app", "Simulation kernel initialization failed: " + runtime_error);
        simulation_kernel_.reset();
        net_service_.reset();
        script_host_.reset();
        world_service_.reset();
        gameplay_fingerprint_.clear();
        loaded_mods_.clear();
        mod_loader_.Shutdown();
        save_repository_.Shutdown();
        sdl_context_.Shutdown();
        return false;
    }

    const script::ScriptRuntimeDescriptor script_runtime_descriptor =
        script_host_->RuntimeDescriptor();
    core::Logger::Info(
        "script",
        "Script runtime active: backend=" + script_runtime_descriptor.backend_name +
            ", api_version=" + script_runtime_descriptor.api_version +
            ", sandbox=" + (script_runtime_descriptor.sandbox_enabled ? "true" : "false"));

    if (has_loaded_save_state) {
        runtime::ApplySaveState(loaded_save_state, *simulation_kernel_, *world_service_);
    }

    player_controller_.Reset();

    initialized_ = true;
    last_net_diagnostics_tick_ = 0;
    core::Logger::Info(
        "input",
        "Player controls active: A/D move, Space jump, mouse-left action hold, mouse-right interaction, 1-0/wheel hotbar, Esc inventory, Tab row, Ctrl smart toggle, Shift smart context, W/S recipe select, Enter craft.");
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

            const PlayerInputIntent player_input_intent =
                input_command_mapper_.Map(frame_actions_);
            player_controller_.Update(
                player_input_intent,
                *world_service_,
                *simulation_kernel_,
                local_player_id_);

            return !quit_requested_;
        },
        [this](double fixed_delta_seconds) {
            simulation_kernel_->Update(fixed_delta_seconds);
            player_controller_.SyncFromSimulation(*simulation_kernel_);

            constexpr std::uint64_t kNetDiagnosticsPeriodTicks = 300;
            const std::uint64_t current_tick = simulation_kernel_->CurrentTick();
            if (current_tick == 0 ||
                current_tick % kNetDiagnosticsPeriodTicks != 0 ||
                current_tick == last_net_diagnostics_tick_) {
                return;
            }

            last_net_diagnostics_tick_ = current_tick;
            const net::NetDiagnosticsSnapshot diagnostics =
                net_service_->DiagnosticsSnapshot();
            core::Logger::Info(
                "net",
                "Diagnostics: tick=" + std::to_string(current_tick) +
                    ", state=" + NetSessionStateName(diagnostics.session_state) +
                    ", last_transition_reason=" + diagnostics.last_session_transition_reason +
                    ", last_heartbeat_tick=" + std::to_string(diagnostics.last_heartbeat_tick) +
                    ", transitions=" + std::to_string(diagnostics.session_transition_count) +
                    ", connected_transitions=" + std::to_string(diagnostics.connected_transition_count) +
                    ", connect_requests=" + std::to_string(diagnostics.connect_request_count) +
                    ", connect_probes(sent/failed)=" +
                    std::to_string(diagnostics.connect_probe_send_count) + "/" +
                    std::to_string(diagnostics.connect_probe_send_failure_count) +
                    ", timeout_disconnects=" + std::to_string(diagnostics.timeout_disconnect_count) +
                    ", manual_disconnects=" + std::to_string(diagnostics.manual_disconnect_count) +
                    ", ignored_senders=" +
                    std::to_string(diagnostics.ignored_unexpected_sender_count) +
                    ", dropped_commands(total/disconnected/queue_full)=" +
                    std::to_string(diagnostics.dropped_command_count) + "/" +
                    std::to_string(diagnostics.dropped_command_disconnected_count) + "/" +
                    std::to_string(diagnostics.dropped_command_queue_full_count) +
                    ", dropped_payloads(total/disconnected/queue_full)=" +
                    std::to_string(diagnostics.dropped_remote_chunk_payload_count) + "/" +
                    std::to_string(diagnostics.dropped_remote_chunk_payload_disconnected_count) + "/" +
                    std::to_string(diagnostics.dropped_remote_chunk_payload_queue_full_count) +
                    ", unsent_commands(total/disconnected/self/send_fail)=" +
                    std::to_string(diagnostics.unsent_command_count) + "/" +
                    std::to_string(diagnostics.unsent_command_disconnected_count) + "/" +
                    std::to_string(diagnostics.unsent_command_self_suppressed_count) + "/" +
                    std::to_string(diagnostics.unsent_command_send_failure_count) +
                    ", unsent_snapshots(total/disconnected/self/send_fail)=" +
                    std::to_string(diagnostics.unsent_snapshot_payload_count) + "/" +
                    std::to_string(diagnostics.unsent_snapshot_disconnected_count) + "/" +
                    std::to_string(diagnostics.unsent_snapshot_self_suppressed_count) + "/" +
                    std::to_string(diagnostics.unsent_snapshot_send_failure_count) +
                    ", ignored_heartbeats=" + std::to_string(diagnostics.ignored_heartbeat_count));
            const sim::GameplayProgressSnapshot gameplay_progress =
                simulation_kernel_->GameplayProgress();
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
        [this](float interpolation_alpha) {
            const float daylight_factor =
                ComputeDaylightFactor(simulation_kernel_->CurrentTick());
            const int viewport_width =
                frame_actions_.viewport_width > 0 ? frame_actions_.viewport_width : config_.window_width;
            const int viewport_height =
                frame_actions_.viewport_height > 0 ? frame_actions_.viewport_height : config_.window_height;
            const platform::RenderScene scene = render_scene_builder_.Build(
                player_controller_.State(),
                viewport_width,
                viewport_height,
                *world_service_,
                daylight_factor);
            sdl_context_.RenderFrame(interpolation_alpha, scene);
        });

    core::Logger::Info("app", "Main loop exited.");
    return 0;
}

void GameApp::Shutdown() {
    if (!initialized_) {
        return;
    }

    std::string save_error;
    const net::NetDiagnosticsSnapshot diagnostics = net_service_->DiagnosticsSnapshot();
    const sim::GameplayProgressSnapshot gameplay_progress =
        simulation_kernel_->GameplayProgress();
    std::vector<wire::ByteBuffer> encoded_world_chunks;
    for (const world::ChunkCoord& chunk_coord : world_service_->LoadedChunkCoords()) {
        world::ChunkSnapshot chunk_snapshot{};
        std::string snapshot_error;
        if (!world_service_->BuildChunkSnapshot(chunk_coord, chunk_snapshot, snapshot_error)) {
            core::Logger::Warn(
                "save",
                "Skip world chunk snapshot build at (" +
                    std::to_string(chunk_coord.x) + "," +
                    std::to_string(chunk_coord.y) + "): " +
                    snapshot_error);
            continue;
        }

        wire::ByteBuffer encoded_chunk;
        if (!world::WorldSnapshotCodec::EncodeChunkSnapshot(
                chunk_snapshot,
                encoded_chunk,
                snapshot_error)) {
            core::Logger::Warn(
                "save",
                "Skip world chunk snapshot encode at (" +
                    std::to_string(chunk_coord.x) + "," +
                    std::to_string(chunk_coord.y) + "): " +
                    snapshot_error);
            continue;
        }

        encoded_world_chunks.push_back(std::move(encoded_chunk));
    }
    const bool has_world_snapshot = !encoded_world_chunks.empty();
    const save::WorldSaveState save_state{
        .tick_index = simulation_kernel_->CurrentTick(),
        .local_player_id = local_player_id_,
        .gameplay_fingerprint = gameplay_fingerprint_,
        .cosmetic_fingerprint = std::string(),
        .gameplay_wood_collected = gameplay_progress.wood_collected,
        .gameplay_stone_collected = gameplay_progress.stone_collected,
        .gameplay_workbench_built = gameplay_progress.workbench_built,
        .gameplay_sword_crafted = gameplay_progress.sword_crafted,
        .gameplay_enemy_kill_count = gameplay_progress.enemy_kill_count,
        .gameplay_boss_health = gameplay_progress.boss_health,
        .gameplay_boss_defeated = gameplay_progress.boss_defeated,
        .gameplay_loop_complete = gameplay_progress.playable_loop_complete,
        .has_gameplay_snapshot = true,
        .world_chunk_payloads = std::move(encoded_world_chunks),
        .has_world_snapshot = has_world_snapshot,
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

    gameplay_fingerprint_.clear();
    loaded_mods_.clear();
    mod_loader_.Shutdown();
    save_repository_.Shutdown();
    simulation_kernel_->Shutdown();
    simulation_kernel_.reset();
    sdl_context_.Shutdown();
    net_service_.reset();
    script_host_.reset();
    world_service_.reset();
    initialized_ = false;
    last_net_diagnostics_tick_ = 0;
    player_controller_.Reset();
    core::Logger::Info("app", "Novaria shutdown complete.");
}

}  // namespace novaria::app
