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
    tick_index_ = 0;
    pending_local_commands_.clear();
    dropped_local_command_count_ = 0;
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
    }
    pending_local_commands_.clear();

    net_service_.Tick(tick_context);
    const net::NetDiagnosticsSnapshot net_diagnostics = net_service_.DiagnosticsSnapshot();
    const net::NetSessionState current_session_state = net_diagnostics.session_state;
    if (current_session_state != last_observed_net_session_state_) {
        script_host_.DispatchEvent(script::ScriptEvent{
            .event_name = "net.session_state_changed",
            .payload = BuildSessionStateChangedPayload(
                current_session_state,
                tick_index_,
                net_diagnostics.last_session_transition_reason),
        });

        if (current_session_state == net::NetSessionState::Disconnected) {
            next_auto_reconnect_tick_ = tick_index_ + kAutoReconnectRetryIntervalTicks;
        }

        last_observed_net_session_state_ = current_session_state;
    }

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
