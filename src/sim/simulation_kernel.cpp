#include "sim/simulation_kernel.h"

#include "sim/command_schema.h"
#include "world/snapshot_codec.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace novaria::sim {
namespace {

constexpr std::uint32_t kGameplayWorkbenchWoodCost = 10;
constexpr std::uint32_t kGameplayWorkbenchStoneCost = 5;
constexpr std::uint32_t kGameplaySwordWoodCost = 8;
constexpr std::uint32_t kGameplaySwordStoneCost = 12;
constexpr std::uint32_t kGameplayBossMaxHealth = 60;
constexpr std::uint32_t kGameplayBossDamagePerAttack = 10;

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

std::string BuildGameplayProgressPayload(
    std::string_view milestone,
    std::uint64_t tick_index) {
    std::string payload = "milestone=";
    payload += milestone;
    payload += ";tick=";
    payload += std::to_string(tick_index);
    return payload;
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

    net_service_.RequestConnect();
    last_observed_net_session_state_ = net_service_.SessionState();
    next_auto_reconnect_tick_ = 0;
    next_net_session_event_dispatch_tick_ = 0;
    pending_net_session_event_ = {};
    tick_index_ = 0;
    pending_local_commands_.clear();
    dropped_local_command_count_ = 0;
    ResetGameplayProgress();
    initialized_ = true;
    out_error.clear();
    return true;
}

void SimulationKernel::Shutdown() {
    if (!initialized_) {
        return;
    }

    script_host_.Shutdown();
    net_service_.Shutdown();
    world_service_.Shutdown();
    pending_local_commands_.clear();
    last_observed_net_session_state_ = net::NetSessionState::Disconnected;
    next_auto_reconnect_tick_ = 0;
    next_net_session_event_dispatch_tick_ = 0;
    pending_net_session_event_ = {};
    ResetGameplayProgress();
    initialized_ = false;
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
    std::string_view encoded_payload,
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

GameplayProgressSnapshot SimulationKernel::GameplayProgress() const {
    return GameplayProgressSnapshot{
        .wood_collected = wood_collected_,
        .stone_collected = stone_collected_,
        .workbench_built = workbench_built_,
        .sword_crafted = sword_crafted_,
        .enemy_kill_count = enemy_kill_count_,
        .boss_health = boss_health_,
        .boss_defeated = boss_defeated_,
        .playable_loop_complete = playable_loop_complete_,
    };
}

void SimulationKernel::RestoreGameplayProgress(const GameplayProgressSnapshot& snapshot) {
    wood_collected_ = snapshot.wood_collected;
    stone_collected_ = snapshot.stone_collected;
    workbench_built_ = snapshot.workbench_built;
    sword_crafted_ = snapshot.sword_crafted;
    enemy_kill_count_ = snapshot.enemy_kill_count;
    boss_health_ = snapshot.boss_health > kGameplayBossMaxHealth
        ? kGameplayBossMaxHealth
        : snapshot.boss_health;
    boss_defeated_ = snapshot.boss_defeated || boss_health_ == 0;
    playable_loop_complete_ =
        snapshot.playable_loop_complete ||
        (workbench_built_ && sword_crafted_ && enemy_kill_count_ >= 3 && boss_defeated_);
}

void SimulationKernel::DispatchGameplayProgressEvent(std::string_view milestone) {
    script_host_.DispatchEvent(script::ScriptEvent{
        .event_name = "gameplay.progress",
        .payload = BuildGameplayProgressPayload(milestone, tick_index_),
    });
}

void SimulationKernel::ResetGameplayProgress() {
    wood_collected_ = 0;
    stone_collected_ = 0;
    workbench_built_ = false;
    sword_crafted_ = false;
    enemy_kill_count_ = 0;
    boss_health_ = kGameplayBossMaxHealth;
    boss_defeated_ = false;
    playable_loop_complete_ = false;
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

void SimulationKernel::ExecuteWorldCommandIfMatched(const net::PlayerCommand& command) {
    if (command.command_type == command::kWorldSetTile) {
        command::WorldSetTilePayload payload{};
        if (!command::TryParseWorldSetTilePayload(command.payload, payload)) {
            return;
        }

        const world::TileMutation mutation{
            .tile_x = payload.tile_x,
            .tile_y = payload.tile_y,
            .material_id = payload.material_id,
        };
        std::string apply_error;
        (void)world_service_.ApplyTileMutation(mutation, apply_error);
        return;
    }

    if (command.command_type == command::kWorldLoadChunk) {
        command::WorldChunkPayload payload{};
        if (!command::TryParseWorldChunkPayload(command.payload, payload)) {
            return;
        }

        world_service_.LoadChunk(world::ChunkCoord{
            .x = payload.chunk_x,
            .y = payload.chunk_y,
        });
        return;
    }

    if (command.command_type == command::kWorldUnloadChunk) {
        command::WorldChunkPayload payload{};
        if (!command::TryParseWorldChunkPayload(command.payload, payload)) {
            return;
        }

        world_service_.UnloadChunk(world::ChunkCoord{
            .x = payload.chunk_x,
            .y = payload.chunk_y,
        });
    }
}

void SimulationKernel::ExecuteGameplayCommandIfMatched(const net::PlayerCommand& command) {
    if (command.command_type == command::kGameplayCollectResource) {
        command::CollectResourcePayload payload{};
        if (!command::TryParseCollectResourcePayload(command.payload, payload)) {
            return;
        }

        if (payload.resource_id == command::kResourceWood) {
            wood_collected_ += payload.amount;
            DispatchGameplayProgressEvent("collect_wood");
            return;
        }

        if (payload.resource_id == command::kResourceStone) {
            stone_collected_ += payload.amount;
            DispatchGameplayProgressEvent("collect_stone");
        }
        return;
    }

    if (command.command_type == command::kGameplayBuildWorkbench) {
        if (workbench_built_) {
            return;
        }
        if (wood_collected_ < kGameplayWorkbenchWoodCost ||
            stone_collected_ < kGameplayWorkbenchStoneCost) {
            return;
        }

        wood_collected_ -= kGameplayWorkbenchWoodCost;
        stone_collected_ -= kGameplayWorkbenchStoneCost;
        workbench_built_ = true;
        DispatchGameplayProgressEvent("build_workbench");
        return;
    }

    if (command.command_type == command::kGameplayCraftSword) {
        if (sword_crafted_) {
            return;
        }
        if (!workbench_built_) {
            return;
        }
        if (wood_collected_ < kGameplaySwordWoodCost ||
            stone_collected_ < kGameplaySwordStoneCost) {
            return;
        }

        wood_collected_ -= kGameplaySwordWoodCost;
        stone_collected_ -= kGameplaySwordStoneCost;
        sword_crafted_ = true;
        DispatchGameplayProgressEvent("craft_sword");
        return;
    }

    if (command.command_type == command::kGameplayAttackEnemy) {
        if (!sword_crafted_) {
            return;
        }

        ++enemy_kill_count_;
        DispatchGameplayProgressEvent("kill_enemy");
        return;
    }

    if (command.command_type == command::kGameplayAttackBoss) {
        if (!sword_crafted_ || boss_defeated_) {
            return;
        }
        if (enemy_kill_count_ < 3) {
            return;
        }

        if (boss_health_ <= kGameplayBossDamagePerAttack) {
            boss_health_ = 0;
            boss_defeated_ = true;
            DispatchGameplayProgressEvent("defeat_boss");
        } else {
            boss_health_ -= kGameplayBossDamagePerAttack;
            DispatchGameplayProgressEvent("attack_boss");
        }
    }
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

    const TickContext tick_context{
        .tick_index = tick_index_,
        .fixed_delta_seconds = fixed_delta_seconds,
    };

    for (const auto& command : pending_local_commands_) {
        net_service_.SubmitLocalCommand(command);
        ExecuteWorldCommandIfMatched(command);
        ExecuteGameplayCommandIfMatched(command);
    }
    pending_local_commands_.clear();

    const bool previous_loop_complete = playable_loop_complete_;
    playable_loop_complete_ =
        workbench_built_ && sword_crafted_ && enemy_kill_count_ >= 3 && boss_defeated_;
    if (playable_loop_complete_ && !previous_loop_complete) {
        DispatchGameplayProgressEvent("playable_loop_complete");
    }

    net_service_.Tick(tick_context);
    const net::NetDiagnosticsSnapshot net_diagnostics = net_service_.DiagnosticsSnapshot();
    const net::NetSessionState current_session_state = net_diagnostics.session_state;
    if (current_session_state != last_observed_net_session_state_) {
        QueueNetSessionChangedEvent(
            current_session_state,
            net_diagnostics.last_session_transition_reason);

        if (current_session_state == net::NetSessionState::Disconnected) {
            next_auto_reconnect_tick_ = tick_index_ + kAutoReconnectRetryIntervalTicks;
        }

        last_observed_net_session_state_ = current_session_state;
    }
    TryDispatchPendingNetSessionEvent();

    const bool net_connected = current_session_state == net::NetSessionState::Connected;
    if (net_connected) {
        for (const auto& encoded_payload : net_service_.ConsumeRemoteChunkPayloads()) {
            std::string apply_error;
            (void)ApplyRemoteChunkPayload(encoded_payload, apply_error);
        }
    }

    world_service_.Tick(tick_context);
    script_host_.Tick(tick_context);

    const auto dirty_chunks = world_service_.ConsumeDirtyChunks();
    std::vector<std::string> encoded_dirty_chunks;
    encoded_dirty_chunks.reserve(dirty_chunks.size());
    for (const auto& chunk_coord : dirty_chunks) {
        world::ChunkSnapshot chunk_snapshot{};
        std::string snapshot_error;
        if (!world_service_.BuildChunkSnapshot(chunk_coord, chunk_snapshot, snapshot_error)) {
            continue;
        }

        std::string encoded_chunk;
        if (!world::WorldSnapshotCodec::EncodeChunkSnapshot(
                chunk_snapshot,
                encoded_chunk,
                snapshot_error)) {
            continue;
        }

        encoded_dirty_chunks.push_back(std::move(encoded_chunk));
    }

    if (net_connected) {
        net_service_.PublishWorldSnapshot(tick_index_, encoded_dirty_chunks);
    }

    ++tick_index_;
}

}  // namespace novaria::sim
