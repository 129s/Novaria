#include "app/game_app.h"

#include "app/game_loop.h"
#include "core/logger.h"
#include "sim/command_schema.h"
#include "world/snapshot_codec.h"

#include <algorithm>
#include <fstream>
#include <iterator>
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

script::ScriptBackendPreference ToScriptBackendPreference(core::ScriptBackendMode mode) {
    switch (mode) {
        case core::ScriptBackendMode::LuaJit:
            return script::ScriptBackendPreference::LuaJit;
    }

    return script::ScriptBackendPreference::LuaJit;
}

net::NetBackendPreference ToNetBackendPreference(core::NetBackendMode mode) {
    switch (mode) {
        case core::NetBackendMode::UdpLoopback:
            return net::NetBackendPreference::UdpLoopback;
    }

    return net::NetBackendPreference::UdpLoopback;
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
    const std::vector<mod::ModManifest>& manifests,
    std::vector<script::ScriptModuleSource>& out_modules,
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

        out_modules.push_back(script::ScriptModuleSource{
            .module_name = manifest.name,
            .api_version =
                manifest.script_api_version.empty()
                    ? std::string(script::kScriptApiVersion)
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

int FloorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

world::ChunkCoord TileToChunkCoord(int tile_x, int tile_y) {
    return world::ChunkCoord{
        .x = FloorDiv(tile_x, world::WorldServiceBasic::kChunkSize),
        .y = FloorDiv(tile_y, world::WorldServiceBasic::kChunkSize),
    };
}

bool IsSolidMaterial(std::uint16_t material_id) {
    return material_id != 0;
}

bool IsCollectibleMaterial(std::uint16_t material_id) {
    return material_id == 1 || material_id == 2;
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
        config_.net_udp_local_host,
        static_cast<std::uint16_t>(config_.net_udp_local_port),
        net::UdpEndpoint{
            .host = config_.net_udp_remote_host,
            .port = static_cast<std::uint16_t>(config_.net_udp_remote_port),
        });
    core::Logger::Info(
        "net",
        "Configured net backend preference: " +
            std::string(core::NetBackendModeName(config_.net_backend_mode)) +
            ", udp_local=" + config_.net_udp_local_host +
            ":" + std::to_string(config_.net_udp_local_port) +
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
            if (loaded_save_state.has_world_snapshot) {
                std::size_t applied_chunk_count = 0;
                for (const std::string& encoded_chunk :
                     loaded_save_state.world_chunk_payloads) {
                    std::string apply_error;
                    if (!simulation_kernel_.ApplyRemoteChunkPayload(
                            encoded_chunk,
                            apply_error)) {
                        core::Logger::Warn(
                            "save",
                            "Skip invalid world chunk snapshot payload: " +
                                apply_error);
                        continue;
                    }
                    ++applied_chunk_count;
                }

                core::Logger::Info(
                    "save",
                    "Loaded world chunk snapshots: applied=" +
                        std::to_string(applied_chunk_count) +
                        ", total=" +
                        std::to_string(loaded_save_state.world_chunk_payloads.size()));
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

    std::vector<script::ScriptModuleSource> script_modules;
    if (!BuildModScriptModules(loaded_mods_, script_modules, runtime_error)) {
        core::Logger::Error("script", "Build mod script modules failed: " + runtime_error);
        mod_manifest_fingerprint_.clear();
        loaded_mods_.clear();
        mod_loader_.Shutdown();
        save_repository_.Shutdown();
        simulation_kernel_.Shutdown();
        sdl_context_.Shutdown();
        return false;
    }
    if (!script_host_.SetScriptModules(std::move(script_modules), runtime_error)) {
        core::Logger::Error("script", "Load mod script modules failed: " + runtime_error);
        mod_manifest_fingerprint_.clear();
        loaded_mods_.clear();
        mod_loader_.Shutdown();
        save_repository_.Shutdown();
        simulation_kernel_.Shutdown();
        sdl_context_.Shutdown();
        return false;
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

    player_tile_x_ = 0;
    player_tile_y_ = -4;
    player_facing_x_ = 1;
    inventory_dirt_count_ = 0;
    inventory_stone_count_ = 0;
    selected_place_material_id_ = 1;
    loaded_chunk_window_ready_ = false;
    loaded_chunk_window_center_x_ = 0;
    loaded_chunk_window_center_y_ = 0;

    initialized_ = true;
    last_net_diagnostics_tick_ = 0;
    core::Logger::Info(
        "input",
        "Player controls active: WASD move, E mine, R place, 1/2 select material.");
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

            auto submit_world_set_tile =
                [this](int tile_x, int tile_y, std::uint16_t material_id) {
                    simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                        .player_id = local_player_id_,
                        .command_type = std::string(sim::command::kWorldSetTile),
                        .payload = sim::command::BuildWorldSetTilePayload(
                            tile_x,
                            tile_y,
                            material_id),
                    });
                };

            auto submit_world_load_chunk = [this](int chunk_x, int chunk_y) {
                simulation_kernel_.SubmitLocalCommand(net::PlayerCommand{
                    .player_id = local_player_id_,
                    .command_type = std::string(sim::command::kWorldLoadChunk),
                    .payload = sim::command::BuildWorldChunkPayload(chunk_x, chunk_y),
                });
            };

            const world::ChunkCoord player_chunk =
                TileToChunkCoord(player_tile_x_, player_tile_y_);
            if (!loaded_chunk_window_ready_ ||
                player_chunk.x != loaded_chunk_window_center_x_ ||
                player_chunk.y != loaded_chunk_window_center_y_) {
                constexpr int kChunkWindowRadius = 2;
                for (int offset_y = -kChunkWindowRadius;
                     offset_y <= kChunkWindowRadius;
                     ++offset_y) {
                    for (int offset_x = -kChunkWindowRadius;
                         offset_x <= kChunkWindowRadius;
                         ++offset_x) {
                        submit_world_load_chunk(
                            player_chunk.x + offset_x,
                            player_chunk.y + offset_y);
                    }
                }

                loaded_chunk_window_ready_ = true;
                loaded_chunk_window_center_x_ = player_chunk.x;
                loaded_chunk_window_center_y_ = player_chunk.y;
            }

            auto try_move_player = [this](int delta_x, int delta_y) {
                const int next_x = player_tile_x_ + delta_x;
                const int next_y = player_tile_y_ + delta_y;
                std::uint16_t destination_material = 0;
                if (world_service_.TryReadTile(next_x, next_y, destination_material) &&
                    IsSolidMaterial(destination_material)) {
                    return;
                }

                player_tile_x_ = next_x;
                player_tile_y_ = next_y;
            };

            if (frame_actions_.move_left) {
                player_facing_x_ = -1;
                try_move_player(-1, 0);
            }
            if (frame_actions_.move_right) {
                player_facing_x_ = 1;
                try_move_player(1, 0);
            }
            if (frame_actions_.move_up) {
                try_move_player(0, -1);
            }
            if (frame_actions_.move_down) {
                try_move_player(0, 1);
            }

            if (frame_actions_.select_material_dirt) {
                selected_place_material_id_ = 1;
            }
            if (frame_actions_.select_material_stone) {
                selected_place_material_id_ = 2;
            }

            const int target_tile_x = player_tile_x_ + player_facing_x_;
            const int target_tile_y = player_tile_y_;
            if (frame_actions_.player_mine) {
                std::uint16_t target_material = 0;
                if (world_service_.TryReadTile(
                        target_tile_x,
                        target_tile_y,
                        target_material) &&
                    IsCollectibleMaterial(target_material)) {
                    submit_world_set_tile(target_tile_x, target_tile_y, 0);
                    if (target_material == 1) {
                        ++inventory_dirt_count_;
                    } else if (target_material == 2) {
                        ++inventory_stone_count_;
                    }
                }
            }

            if (frame_actions_.player_place) {
                std::uint16_t target_material = 0;
                const bool has_target_material =
                    world_service_.TryReadTile(target_tile_x, target_tile_y, target_material);
                if (!has_target_material || !IsSolidMaterial(target_material)) {
                    if (selected_place_material_id_ == 1 && inventory_dirt_count_ > 0) {
                        --inventory_dirt_count_;
                        submit_world_set_tile(target_tile_x, target_tile_y, 1);
                    } else if (
                        selected_place_material_id_ == 2 && inventory_stone_count_ > 0) {
                        --inventory_stone_count_;
                        submit_world_set_tile(target_tile_x, target_tile_y, 2);
                    }
                }
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
        [this](float interpolation_alpha) {
            constexpr int kTilePixelSize = 32;
            platform::RenderScene scene{};
            scene.tile_pixel_size = kTilePixelSize;
            scene.camera_tile_x = player_tile_x_;
            scene.camera_tile_y = player_tile_y_;
            scene.view_tiles_x = std::max(1, config_.window_width / kTilePixelSize);
            scene.view_tiles_y = std::max(1, config_.window_height / kTilePixelSize);
            scene.player_tile_x = player_tile_x_;
            scene.player_tile_y = player_tile_y_;
            scene.hud = platform::RenderHudState{
                .dirt_count = inventory_dirt_count_,
                .stone_count = inventory_stone_count_,
                .selected_material_id = selected_place_material_id_,
            };

            const int first_world_tile_x = scene.camera_tile_x - scene.view_tiles_x / 2;
            const int first_world_tile_y = scene.camera_tile_y - scene.view_tiles_y / 2;
            scene.tiles.reserve(static_cast<std::size_t>(scene.view_tiles_x * scene.view_tiles_y));
            for (int local_y = 0; local_y < scene.view_tiles_y; ++local_y) {
                for (int local_x = 0; local_x < scene.view_tiles_x; ++local_x) {
                    const int world_tile_x = first_world_tile_x + local_x;
                    const int world_tile_y = first_world_tile_y + local_y;
                    std::uint16_t material_id = 0;
                    (void)world_service_.TryReadTile(
                        world_tile_x,
                        world_tile_y,
                        material_id);

                    scene.tiles.push_back(platform::RenderTile{
                        .world_tile_x = world_tile_x,
                        .world_tile_y = world_tile_y,
                        .material_id = material_id,
                    });
                }
            }

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
    const net::NetDiagnosticsSnapshot diagnostics = net_service_.DiagnosticsSnapshot();
    const sim::GameplayProgressSnapshot gameplay_progress = simulation_kernel_.GameplayProgress();
    std::vector<std::string> encoded_world_chunks;
    for (const world::ChunkCoord& chunk_coord : world_service_.LoadedChunkCoords()) {
        world::ChunkSnapshot chunk_snapshot{};
        std::string snapshot_error;
        if (!world_service_.BuildChunkSnapshot(chunk_coord, chunk_snapshot, snapshot_error)) {
            core::Logger::Warn(
                "save",
                "Skip world chunk snapshot build at (" +
                    std::to_string(chunk_coord.x) + "," +
                    std::to_string(chunk_coord.y) + "): " +
                    snapshot_error);
            continue;
        }

        std::string encoded_chunk;
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

    mod_manifest_fingerprint_.clear();
    loaded_mods_.clear();
    mod_loader_.Shutdown();
    save_repository_.Shutdown();
    simulation_kernel_.Shutdown();
    sdl_context_.Shutdown();
    initialized_ = false;
    last_net_diagnostics_tick_ = 0;
    player_tile_x_ = 0;
    player_tile_y_ = -4;
    player_facing_x_ = 1;
    inventory_dirt_count_ = 0;
    inventory_stone_count_ = 0;
    selected_place_material_id_ = 1;
    loaded_chunk_window_ready_ = false;
    loaded_chunk_window_center_x_ = 0;
    loaded_chunk_window_center_y_ = 0;
    core::Logger::Info("app", "Novaria shutdown complete.");
}

}  // namespace novaria::app
