#include "sim/simulation_kernel.h"

#include "world/snapshot_codec.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace novaria::sim {
namespace {

std::vector<std::string> SplitByComma(std::string_view payload) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(payload.size());

    for (const char ch : payload) {
        if (ch == ',') {
            tokens.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    tokens.push_back(current);
    return tokens;
}

bool ParseSignedInt(const std::string& token, int& out_value) {
    try {
        size_t consumed = 0;
        const long long parsed = std::stoll(token, &consumed);
        if (consumed != token.size()) {
            return false;
        }
        if (parsed < std::numeric_limits<int>::min() ||
            parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        out_value = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseMaterialId(const std::string& token, std::uint16_t& out_material_id) {
    try {
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(token, &consumed);
        if (consumed != token.size()) {
            return false;
        }
        if (parsed > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        out_material_id = static_cast<std::uint16_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
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

    tick_index_ = 0;
    pending_local_commands_.clear();
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
    initialized_ = false;
}

void SimulationKernel::SubmitLocalCommand(const net::PlayerCommand& command) {
    if (!initialized_) {
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

bool SimulationKernel::TryParseWorldSetTileCommand(
    const net::PlayerCommand& command,
    world::TileMutation& out_mutation) {
    if (command.command_type != "world.set_tile") {
        return false;
    }

    const auto tokens = SplitByComma(command.payload);
    if (tokens.size() != 3) {
        return false;
    }

    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    if (!ParseSignedInt(tokens[0], tile_x) ||
        !ParseSignedInt(tokens[1], tile_y) ||
        !ParseMaterialId(tokens[2], material_id)) {
        return false;
    }

    out_mutation = world::TileMutation{
        .tile_x = tile_x,
        .tile_y = tile_y,
        .material_id = material_id,
    };
    return true;
}

bool SimulationKernel::TryParseWorldChunkCommand(
    const net::PlayerCommand& command,
    std::string_view expected_command_type,
    world::ChunkCoord& out_chunk_coord) {
    if (command.command_type != expected_command_type) {
        return false;
    }

    const auto tokens = SplitByComma(command.payload);
    if (tokens.size() != 2) {
        return false;
    }

    int chunk_x = 0;
    int chunk_y = 0;
    if (!ParseSignedInt(tokens[0], chunk_x) ||
        !ParseSignedInt(tokens[1], chunk_y)) {
        return false;
    }

    out_chunk_coord = world::ChunkCoord{
        .x = chunk_x,
        .y = chunk_y,
    };
    return true;
}

void SimulationKernel::ExecuteWorldCommandIfMatched(const net::PlayerCommand& command) {
    world::TileMutation mutation{};
    if (TryParseWorldSetTileCommand(command, mutation)) {
        std::string apply_error;
        (void)world_service_.ApplyTileMutation(mutation, apply_error);
        return;
    }

    world::ChunkCoord chunk_coord{};
    if (TryParseWorldChunkCommand(command, "world.load_chunk", chunk_coord)) {
        world_service_.LoadChunk(chunk_coord);
        return;
    }

    if (TryParseWorldChunkCommand(command, "world.unload_chunk", chunk_coord)) {
        world_service_.UnloadChunk(chunk_coord);
    }
}

void SimulationKernel::Update(double fixed_delta_seconds) {
    if (!initialized_) {
        return;
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
    for (const auto& encoded_payload : net_service_.ConsumeRemoteChunkPayloads()) {
        std::string apply_error;
        (void)ApplyRemoteChunkPayload(encoded_payload, apply_error);
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

    net_service_.PublishWorldSnapshot(tick_index_, encoded_dirty_chunks);

    ++tick_index_;
}

}  // namespace novaria::sim
