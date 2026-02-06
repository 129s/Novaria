#include "world/world_service_basic.h"

#include "core/logger.h"

#include <string>

namespace novaria::world {

std::size_t WorldServiceBasic::ChunkKeyHasher::operator()(const ChunkKey& key) const {
    const std::size_t hx = std::hash<int>{}(key.x);
    const std::size_t hy = std::hash<int>{}(key.y);
    return hx ^ (hy + 0x9e3779b9 + (hx << 6) + (hx >> 2));
}

bool WorldServiceBasic::Initialize(std::string& out_error) {
    chunks_.clear();
    initialized_ = true;
    out_error.clear();
    core::Logger::Info("world", "WorldServiceBasic initialized.");
    return true;
}

void WorldServiceBasic::Shutdown() {
    if (!initialized_) {
        return;
    }

    chunks_.clear();
    initialized_ = false;
    core::Logger::Info("world", "WorldServiceBasic shutdown.");
}

void WorldServiceBasic::Tick(const sim::TickContext& tick_context) {
    (void)tick_context;
}

void WorldServiceBasic::LoadChunk(const ChunkCoord& chunk_coord) {
    if (!initialized_) {
        return;
    }

    (void)EnsureChunk(chunk_coord);
}

void WorldServiceBasic::UnloadChunk(const ChunkCoord& chunk_coord) {
    if (!initialized_) {
        return;
    }

    chunks_.erase(ToChunkKey(chunk_coord));
}

bool WorldServiceBasic::ApplyTileMutation(const TileMutation& mutation, std::string& out_error) {
    if (!initialized_) {
        out_error = "World service is not initialized.";
        return false;
    }

    const ChunkCoord chunk_coord = WorldToChunkCoord(mutation.tile_x, mutation.tile_y);
    ChunkData& chunk_data = EnsureChunk(chunk_coord);

    const int local_x = PositiveMod(mutation.tile_x, kChunkSize);
    const int local_y = PositiveMod(mutation.tile_y, kChunkSize);
    const std::size_t local_index = LocalIndex(local_x, local_y);

    chunk_data.tiles[local_index] = mutation.material_id;
    chunk_data.dirty = true;

    out_error.clear();
    return true;
}

std::vector<ChunkCoord> WorldServiceBasic::ConsumeDirtyChunks() {
    std::vector<ChunkCoord> dirty_chunks;
    if (!initialized_) {
        return dirty_chunks;
    }

    for (auto& [chunk_key, chunk_data] : chunks_) {
        if (!chunk_data.dirty) {
            continue;
        }

        dirty_chunks.push_back(ChunkCoord{
            .x = chunk_key.x,
            .y = chunk_key.y,
        });
        chunk_data.dirty = false;
    }

    return dirty_chunks;
}

bool WorldServiceBasic::IsChunkLoaded(const ChunkCoord& chunk_coord) const {
    return FindChunk(chunk_coord) != nullptr;
}

std::size_t WorldServiceBasic::LoadedChunkCount() const {
    return chunks_.size();
}

bool WorldServiceBasic::TryReadTile(int tile_x, int tile_y, std::uint16_t& out_material_id) const {
    const ChunkCoord chunk_coord = WorldToChunkCoord(tile_x, tile_y);
    const ChunkData* chunk_data = FindChunk(chunk_coord);
    if (chunk_data == nullptr) {
        return false;
    }

    const int local_x = PositiveMod(tile_x, kChunkSize);
    const int local_y = PositiveMod(tile_y, kChunkSize);
    const std::size_t local_index = LocalIndex(local_x, local_y);

    out_material_id = chunk_data->tiles[local_index];
    return true;
}

int WorldServiceBasic::FloorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

int WorldServiceBasic::PositiveMod(int value, int divisor) {
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

ChunkCoord WorldServiceBasic::WorldToChunkCoord(int tile_x, int tile_y) {
    return ChunkCoord{
        .x = FloorDiv(tile_x, kChunkSize),
        .y = FloorDiv(tile_y, kChunkSize),
    };
}

std::vector<std::uint16_t> WorldServiceBasic::BuildInitialChunkTiles(const ChunkCoord& chunk_coord) {
    constexpr std::uint16_t kMaterialAir = 0;
    constexpr std::uint16_t kMaterialDirt = 1;
    constexpr std::uint16_t kMaterialStone = 2;

    std::vector<std::uint16_t> tiles(static_cast<std::size_t>(kChunkSize * kChunkSize), kMaterialAir);
    for (int local_y = 0; local_y < kChunkSize; ++local_y) {
        const int world_y = chunk_coord.y * kChunkSize + local_y;
        for (int local_x = 0; local_x < kChunkSize; ++local_x) {
            const std::size_t index = LocalIndex(local_x, local_y);

            if (world_y >= 32) {
                tiles[index] = kMaterialStone;
            } else if (world_y >= 0) {
                tiles[index] = kMaterialDirt;
            } else {
                tiles[index] = kMaterialAir;
            }
        }
    }

    return tiles;
}

std::size_t WorldServiceBasic::LocalIndex(int local_x, int local_y) {
    return static_cast<std::size_t>(local_y * kChunkSize + local_x);
}

WorldServiceBasic::ChunkKey WorldServiceBasic::ToChunkKey(const ChunkCoord& chunk_coord) {
    return ChunkKey{
        .x = chunk_coord.x,
        .y = chunk_coord.y,
    };
}

WorldServiceBasic::ChunkData& WorldServiceBasic::EnsureChunk(const ChunkCoord& chunk_coord) {
    const ChunkKey chunk_key = ToChunkKey(chunk_coord);
    auto [it, inserted] = chunks_.try_emplace(chunk_key);
    if (inserted) {
        it->second.tiles = BuildInitialChunkTiles(chunk_coord);
        it->second.dirty = false;
    }
    return it->second;
}

const WorldServiceBasic::ChunkData* WorldServiceBasic::FindChunk(const ChunkCoord& chunk_coord) const {
    const ChunkKey chunk_key = ToChunkKey(chunk_coord);
    auto it = chunks_.find(chunk_key);
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace novaria::world
