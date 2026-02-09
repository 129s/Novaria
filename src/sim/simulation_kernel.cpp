#include "sim/simulation_kernel.h"

#include "core/logger.h"
#include "script/sim_rules_rpc.h"
#include "world/snapshot_codec.h"
#include "world/material_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace novaria::sim {
namespace {

const char* SessionStateName(net::NetSessionState state) {
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

std::string BuildSessionStateChangedPayload(
    net::NetSessionState state,
    std::uint64_t tick_index,
    std::string_view transition_reason) {
    std::string payload = "state=";
    payload += SessionStateName(state);
    payload += ";tick=";
    payload += std::to_string(tick_index);
    payload += ";reason=";
    payload += transition_reason;
    return payload;
}

bool IsWorkbenchReachable(
    const world::IWorldService& world_service,
    int player_tile_x,
    int player_tile_y,
    int reach_tiles) {
    for (int dy = -reach_tiles; dy <= reach_tiles; ++dy) {
        for (int dx = -reach_tiles; dx <= reach_tiles; ++dx) {
            const int tile_x = player_tile_x + dx;
            const int tile_y = player_tile_y + dy;
            std::uint16_t material_id = 0;
            if (!world_service.TryReadTile(tile_x, tile_y, material_id) ||
                material_id != world::material::kWorkbench) {
                continue;
            }

            if (dx * dx + dy * dy <= reach_tiles * reach_tiles) {
                return true;
            }
        }
    }

    return false;
}

bool IsSameChunkCoord(
    const world::ChunkCoord& lhs,
    const world::ChunkCoord& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

}  // namespace

SimulationKernel::SimulationKernel(
    world::IWorldService& world_service,
    net::INetService& net_service,
    script::IScriptHost& script_host)
    : world_service_(world_service), net_service_(net_service), script_host_(script_host) {}

bool SimulationKernel::Initialize(std::string& out_error) {
    std::string dependency_error;
    if (!world_service_.Initialize(dependency_error)) {
        out_error = "World service initialize failed: " + dependency_error;
        return false;
    }

    if (!net_service_.Initialize(dependency_error)) {
        world_service_.Shutdown();
        out_error = "Net service initialize failed: " + dependency_error;
        return false;
    }

    if (!script_host_.Initialize(dependency_error)) {
        net_service_.Shutdown();
        world_service_.Shutdown();
        out_error = "Script host initialize failed: " + dependency_error;
        return false;
    }
    if (!ecs_runtime_.Initialize(dependency_error)) {
        script_host_.Shutdown();
        net_service_.Shutdown();
        world_service_.Shutdown();
        out_error = "ECS runtime initialize failed: " + dependency_error;
        return false;
    }

    const wire::ByteBuffer validate_request_bytes =
        script::simrpc::EncodeValidateRequest();
    wire::ByteBuffer validate_response;
    if (!script_host_.TryCallModuleFunction(
            "core",
            "novaria_on_sim_command",
            wire::ByteSpan(validate_request_bytes.data(), validate_request_bytes.size()),
            validate_response,
            dependency_error)) {
        ecs_runtime_.Shutdown();
        script_host_.Shutdown();
        net_service_.Shutdown();
        world_service_.Shutdown();
        out_error = "Core script validation failed: " + dependency_error;
        return false;
    }

    script::simrpc::ValidateResponse validate_result{};
    if (!script::simrpc::TryDecodeValidateResponse(
            wire::ByteSpan(validate_response.data(), validate_response.size()),
            validate_result) ||
        !validate_result.ok) {
        ecs_runtime_.Shutdown();
        script_host_.Shutdown();
        net_service_.Shutdown();
        world_service_.Shutdown();
        out_error = "Core script validation failed: invalid response payload.";
        return false;
    }

    net_service_.RequestConnect();
    last_observed_net_session_state_ = net_service_.SessionState();
    next_auto_reconnect_tick_ = 0;
    next_net_session_event_dispatch_tick_ = 0;
    pending_net_session_event_ = {};
    tick_index_ = 0;
    pending_local_commands_.clear();
    pending_pickup_events_.clear();
    dropped_local_command_count_ = 0;
    pending_initial_sync_chunks_.clear();
    gameplay_ruleset_.Reset();
    ecs_runtime_.EnsurePlayer(local_player_id_);
    initialized_ = true;
    out_error.clear();
    return true;
}

void SimulationKernel::Shutdown() {
    if (!initialized_) {
        return;
    }

    ecs_runtime_.Shutdown();
    script_host_.Shutdown();
    net_service_.Shutdown();
    world_service_.Shutdown();
    pending_local_commands_.clear();
    pending_pickup_events_.clear();
    last_observed_net_session_state_ = net::NetSessionState::Disconnected;
    next_auto_reconnect_tick_ = 0;
    next_net_session_event_dispatch_tick_ = 0;
    pending_net_session_event_ = {};
    pending_initial_sync_chunks_.clear();
    gameplay_ruleset_.Reset();
    initialized_ = false;
}

void SimulationKernel::SetLocalPlayerId(std::uint32_t player_id) {
    if (player_id == 0) {
        return;
    }

    local_player_id_ = player_id;
    if (initialized_) {
        ecs_runtime_.EnsurePlayer(local_player_id_);
    }
}

std::uint32_t SimulationKernel::LocalPlayerId() const {
    return local_player_id_;
}

void SimulationKernel::SetAuthorityMode(SimulationAuthorityMode authority_mode) {
    authority_mode_ = authority_mode;
}

SimulationAuthorityMode SimulationKernel::AuthorityMode() const {
    return authority_mode_;
}

void SimulationKernel::SubmitLocalCommand(const net::PlayerCommand& command) {
    if (!initialized_) {
        return;
    }

    if (pending_local_commands_.size() >= kMaxPendingLocalCommands) {
        ++dropped_local_command_count_;
        return;
    }

    pending_local_commands_.push_back(command);
}

bool SimulationKernel::ApplyRemoteChunkPayload(
    wire::ByteSpan encoded_payload,
    std::string& out_error) {
    if (!initialized_) {
        out_error = "Simulation kernel is not initialized.";
        return false;
    }

    world::ChunkSnapshot snapshot{};
    if (!world::WorldSnapshotCodec::DecodeChunkSnapshot(encoded_payload, snapshot, out_error)) {
        return false;
    }

    return world_service_.ApplyChunkSnapshot(snapshot, out_error);
}

std::uint64_t SimulationKernel::CurrentTick() const {
    return tick_index_;
}

std::size_t SimulationKernel::PendingLocalCommandCount() const {
    return pending_local_commands_.size();
}

std::size_t SimulationKernel::DroppedLocalCommandCount() const {
    return dropped_local_command_count_;
}

std::vector<GameplayPickupEvent> SimulationKernel::ConsumePickupEventsForPlayer(
    std::uint32_t player_id) {
    std::vector<GameplayPickupEvent> events;
    if (pending_pickup_events_.empty()) {
        return events;
    }

    auto write_iter = pending_pickup_events_.begin();
    for (auto read_iter = pending_pickup_events_.begin();
         read_iter != pending_pickup_events_.end();
         ++read_iter) {
        if (read_iter->player_id == player_id) {
            events.push_back(*read_iter);
            continue;
        }

        if (write_iter != read_iter) {
            *write_iter = std::move(*read_iter);
        }
        ++write_iter;
    }

    pending_pickup_events_.erase(write_iter, pending_pickup_events_.end());
    return events;
}

GameplayProgressSnapshot SimulationKernel::GameplayProgress() const {
    return gameplay_ruleset_.Snapshot();
}

PlayerInventorySnapshot SimulationKernel::InventorySnapshot(std::uint32_t player_id) const {
    return ecs_runtime_.InventorySnapshot(player_id);
}

PlayerMotionSnapshot SimulationKernel::LocalPlayerMotion() const {
    if (!initialized_) {
        return PlayerMotionSnapshot{};
    }

    return ecs_runtime_.MotionSnapshot(local_player_id_);
}

void SimulationKernel::RestoreGameplayProgress(const GameplayProgressSnapshot& snapshot) {
    gameplay_ruleset_.Restore(snapshot);
}

void SimulationKernel::QueueNetSessionChangedEvent(
    net::NetSessionState session_state,
    std::string_view transition_reason) {
    pending_net_session_event_.has_value = true;
    pending_net_session_event_.session_state = session_state;
    pending_net_session_event_.transition_tick = tick_index_;
    pending_net_session_event_.transition_reason.assign(transition_reason);
}

void SimulationKernel::TryDispatchPendingNetSessionEvent() {
    if (!pending_net_session_event_.has_value ||
        tick_index_ < next_net_session_event_dispatch_tick_) {
        return;
    }

    script_host_.DispatchEvent(script::ScriptEvent{
        .event_name = "net.session_state_changed",
        .payload = BuildSessionStateChangedPayload(
            pending_net_session_event_.session_state,
            pending_net_session_event_.transition_tick,
            pending_net_session_event_.transition_reason),
    });

    pending_net_session_event_ = {};
    next_net_session_event_dispatch_tick_ =
        tick_index_ + kSessionStateEventMinIntervalTicks;
}

void SimulationKernel::QueueChunkForInitialSync(const world::ChunkCoord& chunk_coord) {
    const bool already_queued =
        std::any_of(
            pending_initial_sync_chunks_.begin(),
            pending_initial_sync_chunks_.end(),
            [&chunk_coord](const world::ChunkCoord& queued_chunk) {
                return IsSameChunkCoord(queued_chunk, chunk_coord);
            });
    if (!already_queued) {
        pending_initial_sync_chunks_.push_back(chunk_coord);
    }
}

void SimulationKernel::RemoveChunkFromInitialSync(const world::ChunkCoord& chunk_coord) {
    pending_initial_sync_chunks_.erase(
        std::remove_if(
            pending_initial_sync_chunks_.begin(),
            pending_initial_sync_chunks_.end(),
            [&chunk_coord](const world::ChunkCoord& queued_chunk) {
                return IsSameChunkCoord(queued_chunk, chunk_coord);
            }),
        pending_initial_sync_chunks_.end());
}

void SimulationKernel::QueueLoadedChunksForInitialSync() {
    for (const world::ChunkCoord& chunk_coord : world_service_.LoadedChunkCoords()) {
        QueueChunkForInitialSync(chunk_coord);
    }
}

void SimulationKernel::ExecuteWorldCommandIfMatched(const TypedPlayerCommand& command) {
    if (command.type == TypedPlayerCommandType::WorldSetTile) {
        const world::TileMutation mutation{
            .tile_x = command.world_set_tile.tile_x,
            .tile_y = command.world_set_tile.tile_y,
            .material_id = command.world_set_tile.material_id,
        };
        std::string apply_error;
        (void)world_service_.ApplyTileMutation(mutation, apply_error);
        return;
    }

    if (command.type == TypedPlayerCommandType::WorldLoadChunk) {
        const world::ChunkCoord chunk_coord{
            .x = command.world_chunk.chunk_x,
            .y = command.world_chunk.chunk_y,
        };
        world_service_.LoadChunk(chunk_coord);
        QueueChunkForInitialSync(chunk_coord);
        return;
    }

    if (command.type == TypedPlayerCommandType::WorldUnloadChunk) {
        const world::ChunkCoord chunk_coord{
            .x = command.world_chunk.chunk_x,
            .y = command.world_chunk.chunk_y,
        };
        world_service_.UnloadChunk(chunk_coord);
        RemoveChunkFromInitialSync(chunk_coord);
    }
}

void SimulationKernel::ExecuteControlCommandIfMatched(
    const TypedPlayerCommand& command,
    std::uint32_t player_id) {
    if (command.type != TypedPlayerCommandType::PlayerMotionInput) {
        return;
    }

    PlayerMotionInput input{};
    input.move_axis = static_cast<float>(command.player_motion_input.move_axis_milli) / 1000.0F;
    input.jump_pressed = (command.player_motion_input.input_flags & command::kMotionInputFlagJumpPressed) != 0;
    ecs_runtime_.SetPlayerMotionInput(player_id, input);
}

void SimulationKernel::ExecuteGameplayCommandIfMatched(
    const TypedPlayerCommand& command,
    std::uint32_t player_id) {
    if (command.type == TypedPlayerCommandType::GameplayCollectResource) {
        ecs_runtime_.AddResourceToInventory(
            player_id,
            command.collect_resource.resource_id,
            command.collect_resource.amount);

        gameplay_ruleset_.CollectResource(
            command.collect_resource.resource_id,
            command.collect_resource.amount,
            tick_index_,
            script_host_);
        return;
    }

    if (command.type == TypedPlayerCommandType::GameplaySpawnDrop) {
        ecs_runtime_.QueueSpawnWorldDrop(command.spawn_drop);
        return;
    }

    if (command.type == TypedPlayerCommandType::GameplayPickupProbe) {
        ecs_runtime_.QueuePickupProbe(player_id, command.pickup_probe);
        return;
    }

    if (command.type == TypedPlayerCommandType::GameplayInteraction) {
        gameplay_ruleset_.ExecuteInteraction(
            player_id,
            command.interaction,
            tick_index_,
            script_host_);
        return;
    }

    if (command.type == TypedPlayerCommandType::GameplayActionPrimary) {
        ecs::ActionPrimaryPlan plan{};
        std::uint16_t target_material_id = 0;
        if (!world_service_.TryReadTile(
                command.action_primary.target_tile_x,
                command.action_primary.target_tile_y,
                target_material_id)) {
            ecs_runtime_.ApplyActionPrimary(
                player_id,
                command.action_primary,
                plan,
                0,
                world_service_);
            return;
        }

        const PlayerMotionSnapshot motion_snapshot = ecs_runtime_.MotionSnapshot(player_id);
        const int player_tile_x = static_cast<int>(std::floor(motion_snapshot.position_x));
        const int player_tile_y = static_cast<int>(std::floor(motion_snapshot.position_y));
        const PlayerInventorySnapshot inventory = ecs_runtime_.InventorySnapshot(player_id);

        const script::simrpc::ActionPrimaryRequest request_data{
            .player_id = player_id,
            .player_tile_x = player_tile_x,
            .player_tile_y = player_tile_y,
            .target_tile_x = command.action_primary.target_tile_x,
            .target_tile_y = command.action_primary.target_tile_y,
            .hotbar_row = command.action_primary.hotbar_row,
            .hotbar_slot = command.action_primary.hotbar_slot,
            .dirt_count = inventory.dirt_count,
            .stone_count = inventory.stone_count,
            .wood_count = inventory.wood_count,
            .coal_count = inventory.coal_count,
            .torch_count = inventory.torch_count,
            .workbench_count = inventory.workbench_count,
            .wood_sword_count = inventory.wood_sword_count,
            .has_pickaxe_tool = inventory.has_pickaxe_tool,
            .has_axe_tool = inventory.has_axe_tool,
            .target_is_air = target_material_id == world::material::kAir,
            .harvest_ticks = static_cast<std::uint32_t>(world::material::HarvestTicks(target_material_id)),
            .harvestable_by_pickaxe = world::material::IsHarvestableByPickaxe(target_material_id),
            .harvestable_by_axe = world::material::IsHarvestableByAxe(target_material_id),
            .harvestable_by_sword = world::material::IsHarvestableBySword(target_material_id),
        };

        const wire::ByteBuffer request_bytes =
            script::simrpc::EncodeActionPrimaryRequest(request_data);

        wire::ByteBuffer response;
        std::string call_error;
        if (!script_host_.TryCallModuleFunction(
                "core",
                "novaria_on_sim_command",
                wire::ByteSpan(request_bytes.data(), request_bytes.size()),
                response,
                call_error)) {
            core::Logger::Error("sim", "Core script call failed: " + call_error);
            std::abort();
        }

        script::simrpc::ActionPrimaryResponse response_data{};
        if (!script::simrpc::TryDecodeActionPrimaryResponse(
                wire::ByteSpan(response.data(), response.size()),
                response_data)) {
            core::Logger::Error("sim", "Invalid core script action_primary response payload.");
            std::abort();
        }

        if (response_data.result == script::simrpc::ActionPrimaryResult::Harvest) {
            if (response_data.required_ticks == 0) {
                core::Logger::Error("sim", "Core script returned invalid harvest ticks.");
                std::abort();
            }
            plan.is_harvest = true;
            plan.required_ticks = static_cast<int>(response_data.required_ticks);
        } else if (response_data.result == script::simrpc::ActionPrimaryResult::Place) {
            if (response_data.required_ticks == 0) {
                core::Logger::Error("sim", "Core script returned invalid place ticks.");
                std::abort();
            }

            std::uint16_t place_material_id = 0;
            switch (response_data.place_kind) {
                case script::simrpc::PlaceKind::Dirt:
                    place_material_id = world::material::kDirt;
                    break;
                case script::simrpc::PlaceKind::Stone:
                    place_material_id = world::material::kStone;
                    break;
                case script::simrpc::PlaceKind::Torch:
                    place_material_id = world::material::kTorch;
                    break;
                case script::simrpc::PlaceKind::Workbench:
                    place_material_id = world::material::kWorkbench;
                    break;
                default:
                    break;
            }
            if (place_material_id == 0) {
                core::Logger::Error("sim", "Core script returned invalid place kind.");
                std::abort();
            }

            plan.is_place = true;
            plan.place_material_id = place_material_id;
            plan.required_ticks = static_cast<int>(response_data.required_ticks);
        } else {
            plan = {};
        }

        ecs_runtime_.ApplyActionPrimary(
            player_id,
            command.action_primary,
            plan,
            target_material_id,
            world_service_);
        return;
    }

    if (command.type == TypedPlayerCommandType::GameplayCraftRecipe) {
        const PlayerMotionSnapshot motion_snapshot = ecs_runtime_.MotionSnapshot(player_id);
        const int player_tile_x = static_cast<int>(std::floor(motion_snapshot.position_x));
        const int player_tile_y = static_cast<int>(std::floor(motion_snapshot.position_y));
        const PlayerInventorySnapshot inventory = ecs_runtime_.InventorySnapshot(player_id);

        constexpr int kWorkbenchReach = 4;
        const bool workbench_reachable =
            IsWorkbenchReachable(world_service_, player_tile_x, player_tile_y, kWorkbenchReach);

        const script::simrpc::CraftRecipeRequest request_data{
            .player_id = player_id,
            .player_tile_x = player_tile_x,
            .player_tile_y = player_tile_y,
            .recipe_index = command.craft_recipe.recipe_index,
            .workbench_reachable = workbench_reachable,
            .dirt_count = inventory.dirt_count,
            .stone_count = inventory.stone_count,
            .wood_count = inventory.wood_count,
            .coal_count = inventory.coal_count,
            .torch_count = inventory.torch_count,
            .workbench_count = inventory.workbench_count,
            .wood_sword_count = inventory.wood_sword_count,
        };

        const wire::ByteBuffer request_bytes =
            script::simrpc::EncodeCraftRecipeRequest(request_data);

        wire::ByteBuffer response;
        std::string call_error;
        if (!script_host_.TryCallModuleFunction(
                "core",
                "novaria_on_sim_command",
                wire::ByteSpan(request_bytes.data(), request_bytes.size()),
                response,
                call_error)) {
            core::Logger::Error("sim", "Core script call failed: " + call_error);
            std::abort();
        }

        script::simrpc::CraftRecipeResponse response_data{};
        if (!script::simrpc::TryDecodeCraftRecipeResponse(
                wire::ByteSpan(response.data(), response.size()),
                response_data)) {
            core::Logger::Error("sim", "Invalid core script craft_recipe response payload.");
            std::abort();
        }

        if (response_data.result != script::simrpc::CraftRecipeResult::Craft) {
            return;
        }

        ecs::CraftRecipePlan plan{};
        plan.dirt_delta = response_data.dirt_delta;
        plan.stone_delta = response_data.stone_delta;
        plan.wood_delta = response_data.wood_delta;
        plan.coal_delta = response_data.coal_delta;
        plan.torch_delta = response_data.torch_delta;
        plan.workbench_delta = response_data.workbench_delta;
        plan.wood_sword_delta = response_data.wood_sword_delta;
        plan.mark_workbench_built = response_data.mark_workbench_built;
        plan.mark_sword_crafted = response_data.mark_sword_crafted;
        if (response_data.crafted_kind == script::simrpc::CraftedKind::Workbench) {
            plan.crafted_material_id = world::material::kWorkbench;
        } else if (response_data.crafted_kind == script::simrpc::CraftedKind::Torch) {
            plan.crafted_material_id = world::material::kTorch;
        } else {
            plan.crafted_material_id = 0;
        }

        std::uint16_t out_crafted_material_id = 0;
        const bool crafted = ecs_runtime_.TryCraftRecipePlan(player_id, plan, out_crafted_material_id);
        (void)out_crafted_material_id;
        if (crafted) {
            if (plan.mark_workbench_built) {
                gameplay_ruleset_.MarkWorkbenchBuilt(tick_index_, script_host_);
            }
            if (plan.mark_sword_crafted) {
                gameplay_ruleset_.MarkSwordCrafted(tick_index_, script_host_);
            }
        }
        return;
    }

    if (command.type == TypedPlayerCommandType::GameplayAttackEnemy) {
        gameplay_ruleset_.ExecuteAttackEnemy(tick_index_, script_host_);
        return;
    }

    if (command.type == TypedPlayerCommandType::GameplayAttackBoss) {
        gameplay_ruleset_.ExecuteAttackBoss(tick_index_, script_host_);
    }
}

void SimulationKernel::ExecuteCombatCommandIfMatched(
    const TypedPlayerCommand& command,
    std::uint32_t player_id) {
    if (command.type != TypedPlayerCommandType::CombatFireProjectile) {
        return;
    }

    ecs_runtime_.QueueSpawnProjectile(player_id, command.fire_projectile);
}

void SimulationKernel::Update(double fixed_delta_seconds) {
    if (!initialized_) {
        return;
    }

    if (net_service_.SessionState() == net::NetSessionState::Disconnected &&
        tick_index_ >= next_auto_reconnect_tick_) {
        net_service_.RequestConnect();
        next_auto_reconnect_tick_ = tick_index_ + kAutoReconnectRetryIntervalTicks;
    }

    const core::TickContext tick_context{
        .tick_index = tick_index_,
        .fixed_delta_seconds = fixed_delta_seconds,
    };
    const bool authority_mode = authority_mode_ == SimulationAuthorityMode::Authority;

    for (const auto& command : pending_local_commands_) {
        net_service_.SubmitLocalCommand(command);
    }
    pending_local_commands_.clear();

    net_service_.Tick(tick_context);
    const std::vector<net::PlayerCommand> remote_commands = net_service_.ConsumeRemoteCommands();
    if (authority_mode) {
        for (const net::PlayerCommand& command : remote_commands) {
            TypedPlayerCommand typed_command{};
            if (!TryDecodePlayerCommand(command, typed_command)) {
                continue;
            }

            ExecuteControlCommandIfMatched(typed_command, command.player_id);
            ExecuteWorldCommandIfMatched(typed_command);
            ExecuteGameplayCommandIfMatched(typed_command, command.player_id);
            ExecuteCombatCommandIfMatched(typed_command, command.player_id);
        }
    }

    const net::NetDiagnosticsSnapshot net_diagnostics = net_service_.DiagnosticsSnapshot();
    const net::NetSessionState current_session_state = net_diagnostics.session_state;
    if (current_session_state != last_observed_net_session_state_) {
        QueueNetSessionChangedEvent(
            current_session_state,
            net_diagnostics.last_session_transition_reason);
        if (authority_mode && current_session_state == net::NetSessionState::Connected) {
            QueueLoadedChunksForInitialSync();
        }

        if (current_session_state == net::NetSessionState::Disconnected) {
            next_auto_reconnect_tick_ = tick_index_ + kAutoReconnectRetryIntervalTicks;
        }

        last_observed_net_session_state_ = current_session_state;
    }
    TryDispatchPendingNetSessionEvent();

    const bool net_connected = current_session_state == net::NetSessionState::Connected;
    if (net_connected && !authority_mode) {
        for (const auto& encoded_payload : net_service_.ConsumeRemoteChunkPayloads()) {
            std::string apply_error;
            (void)ApplyRemoteChunkPayload(
                wire::ByteSpan(encoded_payload.data(), encoded_payload.size()),
                apply_error);
        }
    }

    world_service_.Tick(tick_context);
    ecs_runtime_.Tick(tick_context, world_service_);
    gameplay_ruleset_.ProcessCombatEvents(
        ecs_runtime_.ConsumeCombatEvents(),
        tick_index_,
        script_host_);
    const std::size_t pickup_insert_offset = pending_pickup_events_.size();
    gameplay_ruleset_.ProcessGameplayEvents(
        ecs_runtime_.ConsumeGameplayEvents(),
        tick_index_,
        script_host_,
        pending_pickup_events_);
    for (std::size_t index = pickup_insert_offset; index < pending_pickup_events_.size(); ++index) {
        const GameplayPickupEvent& pickup_event = pending_pickup_events_[index];
        if (pickup_event.resource_id != 0) {
            gameplay_ruleset_.CollectResource(
                pickup_event.resource_id,
                pickup_event.amount,
                tick_index_,
                script_host_);
        }
    }
    script_host_.Tick(tick_context);

    std::vector<wire::ByteBuffer> encoded_dirty_chunks;
    if (net_connected && authority_mode) {
        std::vector<world::ChunkCoord> chunks_to_publish = world_service_.ConsumeDirtyChunks();
        chunks_to_publish.insert(
            chunks_to_publish.end(),
            pending_initial_sync_chunks_.begin(),
            pending_initial_sync_chunks_.end());
        pending_initial_sync_chunks_.clear();
        std::sort(
            chunks_to_publish.begin(),
            chunks_to_publish.end(),
            [](const world::ChunkCoord& lhs, const world::ChunkCoord& rhs) {
                if (lhs.x != rhs.x) {
                    return lhs.x < rhs.x;
                }
                return lhs.y < rhs.y;
            });
        chunks_to_publish.erase(
            std::unique(
                chunks_to_publish.begin(),
                chunks_to_publish.end(),
                [](const world::ChunkCoord& lhs, const world::ChunkCoord& rhs) {
                    return IsSameChunkCoord(lhs, rhs);
                }),
            chunks_to_publish.end());

        encoded_dirty_chunks.reserve(chunks_to_publish.size());
        for (const world::ChunkCoord& chunk_coord : chunks_to_publish) {
            world::ChunkSnapshot chunk_snapshot{};
            std::string snapshot_error;
            if (!world_service_.BuildChunkSnapshot(chunk_coord, chunk_snapshot, snapshot_error)) {
                continue;
            }

            wire::ByteBuffer encoded_chunk;
            if (!world::WorldSnapshotCodec::EncodeChunkSnapshot(
                    chunk_snapshot,
                    encoded_chunk,
                    snapshot_error)) {
                continue;
            }

            encoded_dirty_chunks.push_back(std::move(encoded_chunk));
        }

        net_service_.PublishWorldSnapshot(tick_index_, encoded_dirty_chunks);
    }

    ++tick_index_;
}

}  // namespace novaria::sim
