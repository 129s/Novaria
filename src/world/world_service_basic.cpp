#include "world/world_service_basic.h"

#include "core/logger.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace novaria::world {
namespace {

constexpr std::uint32_t kWorldSeed = 0x9e3779b9U;
constexpr std::uint32_t kCoalOreSeed = 0x13f4a8d1U;
constexpr int kTerrainSegmentWidth = 16;
constexpr int kBaseSurfaceY = -1;
constexpr int kTopSoilDepth = 5;
constexpr int kLakeSurfaceY = 0;

int FloorDivInt(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        return quotient - 1;
    }
    return quotient;
}

int PositiveModInt(int value, int divisor) {
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

std::uint32_t MixHash32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

std::uint32_t HashCoords(int x, int y, std::uint32_t seed) {
    std::uint32_t hash = seed;
    hash ^= MixHash32(static_cast<std::uint32_t>(x) + 0x9e3779b9U);
    hash ^= MixHash32(static_cast<std::uint32_t>(y) + 0x85ebca6bU);
    return MixHash32(hash);
}

int SurfaceOffsetForSegment(int segment_x) {
    if (segment_x == 0) {
        return 0;
    }

    const std::uint32_t hash = HashCoords(segment_x, 0, kWorldSeed);
    return static_cast<int>(hash % 6U) - 2;
}

int SurfaceHeightAt(int world_tile_x) {
    const int segment_x = FloorDivInt(world_tile_x, kTerrainSegmentWidth);
    const int local_x = PositiveModInt(world_tile_x, kTerrainSegmentWidth);
    const int current_offset = SurfaceOffsetForSegment(segment_x);
    const int next_offset = SurfaceOffsetForSegment(segment_x + 1);
    const int weighted_offset =
        (current_offset * (kTerrainSegmentWidth - local_x) + next_offset * local_x) /
        kTerrainSegmentWidth;
    return kBaseSurfaceY + weighted_offset;
}

bool ShouldSpawnTreeAt(int world_tile_x, int surface_y) {
    if (surface_y > kLakeSurfaceY + 1) {
        return false;
    }
    if (world_tile_x == 2) {
        return true;
    }

    constexpr std::uint32_t kTreeSeed = 0x4f1bbcdcU;
    const std::uint32_t hash = HashCoords(world_tile_x, surface_y, kTreeSeed);
    if (hash % 13U != 0U) {
        return false;
    }

    const std::uint32_t left_hash =
        HashCoords(world_tile_x - 2, SurfaceHeightAt(world_tile_x - 2), kTreeSeed);
    return left_hash % 13U != 0U;
}

int TreeHeightAt(int world_tile_x) {
    constexpr std::uint32_t kTreeHeightSeed = 0x6c8e9cf5U;
    return 4 + static_cast<int>(HashCoords(world_tile_x, 7, kTreeHeightSeed) % 2U);
}

bool TryResolveTreeMaterial(
    int world_tile_x,
    int world_tile_y,
    std::uint16_t& out_material_id) {
    for (int root_x = world_tile_x - 2; root_x <= world_tile_x + 2; ++root_x) {
        const int root_surface_y = SurfaceHeightAt(root_x);
        if (!ShouldSpawnTreeAt(root_x, root_surface_y)) {
            continue;
        }

        const int tree_height = TreeHeightAt(root_x);
        const int trunk_top_y = root_surface_y - tree_height;
        if (world_tile_x == root_x &&
            world_tile_y < root_surface_y &&
            world_tile_y >= trunk_top_y) {
            out_material_id = WorldServiceBasic::kMaterialWood;
            return true;
        }

        const int leaf_center_y = trunk_top_y;
        const int dx = std::abs(world_tile_x - root_x);
        const int dy = std::abs(world_tile_y - leaf_center_y);
        const bool in_leaf_bounds =
            dx <= 2 && dy <= 2 && !(dx == 2 && dy == 2) && world_tile_y < root_surface_y;
        if (in_leaf_bounds) {
            out_material_id = WorldServiceBasic::kMaterialLeaves;
            return true;
        }
    }

    return false;
}

std::uint16_t GenerateInitialMaterial(int world_tile_x, int world_tile_y) {
    int surface_y = SurfaceHeightAt(world_tile_x);
    if (world_tile_x >= 20 && world_tile_x <= 28) {
        surface_y = std::max(surface_y, 3);
    }

    if (world_tile_y < surface_y) {
        std::uint16_t tree_material = WorldServiceBasic::kMaterialAir;
        if (TryResolveTreeMaterial(world_tile_x, world_tile_y, tree_material)) {
            return tree_material;
        }

        if (world_tile_y >= kLakeSurfaceY &&
            world_tile_y < surface_y &&
            surface_y >= kLakeSurfaceY + 2) {
            return WorldServiceBasic::kMaterialWater;
        }

        return WorldServiceBasic::kMaterialAir;
    }

    if (world_tile_y == surface_y) {
        return WorldServiceBasic::kMaterialGrass;
    }
    if (world_tile_y < surface_y + kTopSoilDepth) {
        return WorldServiceBasic::kMaterialDirt;
    }
    if (world_tile_y >= surface_y + kTopSoilDepth + 2) {
        const std::uint32_t ore_hash = HashCoords(world_tile_x, world_tile_y, kCoalOreSeed);
        if (ore_hash % 17U == 0U) {
            return WorldServiceBasic::kMaterialCoalOre;
        }
    }
    return WorldServiceBasic::kMaterialStone;
}

}  // namespace

std::size_t WorldServiceBasic::ChunkKeyHasher::operator()(const ChunkKey& key) const {
    const std::size_t hx = std::hash<int>{}(key.x);
    const std::size_t hy = std::hash<int>{}(key.y);
    return hx ^ (hy + 0x9e3779b9 + (hx << 6) + (hx >> 2));
}

bool WorldServiceBasic::Initialize(std::string& out_error) {
    chunks_.clear();
    dirty_chunk_keys_.clear();
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
    dirty_chunk_keys_.clear();
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

    const ChunkKey chunk_key = ToChunkKey(chunk_coord);
    chunks_.erase(chunk_key);
    dirty_chunk_keys_.erase(chunk_key);
}

bool WorldServiceBasic::ApplyTileMutation(const TileMutation& mutation, std::string& out_error) {
    if (!initialized_) {
        out_error = "World service is not initialized.";
        return false;
    }

    const ChunkCoord chunk_coord = WorldToChunkCoord(mutation.tile_x, mutation.tile_y);
    const ChunkKey chunk_key = ToChunkKey(chunk_coord);
    ChunkData& chunk_data = EnsureChunk(chunk_coord);

    const int local_x = PositiveMod(mutation.tile_x, kChunkSize);
    const int local_y = PositiveMod(mutation.tile_y, kChunkSize);
    const std::size_t local_index = LocalIndex(local_x, local_y);

    chunk_data.tiles[local_index] = mutation.material_id;
    if (!chunk_data.dirty) {
        chunk_data.dirty = true;
        dirty_chunk_keys_.insert(chunk_key);
    }

    out_error.clear();
    return true;
}

bool WorldServiceBasic::BuildChunkSnapshot(
    const ChunkCoord& chunk_coord,
    ChunkSnapshot& out_snapshot,
    std::string& out_error) const {
    if (!initialized_) {
        out_error = "World service is not initialized.";
        return false;
    }

    const ChunkData* chunk_data = FindChunk(chunk_coord);
    if (chunk_data == nullptr) {
        out_error = "Chunk is not loaded.";
        return false;
    }

    out_snapshot.chunk_coord = chunk_coord;
    out_snapshot.tiles = chunk_data->tiles;
    out_error.clear();
    return true;
}

bool WorldServiceBasic::ApplyChunkSnapshot(const ChunkSnapshot& snapshot, std::string& out_error) {
    if (!initialized_) {
        out_error = "World service is not initialized.";
        return false;
    }

    const std::size_t expected_tile_count = static_cast<std::size_t>(kChunkSize * kChunkSize);
    if (snapshot.tiles.size() != expected_tile_count) {
        out_error = "Snapshot tile count does not match chunk size.";
        return false;
    }

    ChunkData& chunk_data = EnsureChunk(snapshot.chunk_coord);
    chunk_data.tiles = snapshot.tiles;
    chunk_data.dirty = false;
    dirty_chunk_keys_.erase(ToChunkKey(snapshot.chunk_coord));
    out_error.clear();
    return true;
}

std::vector<ChunkCoord> WorldServiceBasic::ConsumeDirtyChunks() {
    std::vector<ChunkCoord> dirty_chunks;
    if (!initialized_) {
        return dirty_chunks;
    }

    dirty_chunks.reserve(dirty_chunk_keys_.size());
    for (const ChunkKey& chunk_key : dirty_chunk_keys_) {
        auto chunk_iter = chunks_.find(chunk_key);
        if (chunk_iter == chunks_.end()) {
            continue;
        }

        chunk_iter->second.dirty = false;
        dirty_chunks.push_back(ChunkCoord{
            .x = chunk_key.x,
            .y = chunk_key.y,
        });
    }
    dirty_chunk_keys_.clear();
    std::sort(
        dirty_chunks.begin(),
        dirty_chunks.end(),
        [](const ChunkCoord& lhs, const ChunkCoord& rhs) {
            if (lhs.x != rhs.x) {
                return lhs.x < rhs.x;
            }
            return lhs.y < rhs.y;
        });

    return dirty_chunks;
}

bool WorldServiceBasic::IsChunkLoaded(const ChunkCoord& chunk_coord) const {
    return FindChunk(chunk_coord) != nullptr;
}

std::size_t WorldServiceBasic::LoadedChunkCount() const {
    return chunks_.size();
}

std::vector<ChunkCoord> WorldServiceBasic::LoadedChunkCoords() const {
    std::vector<ChunkCoord> chunk_coords;
    chunk_coords.reserve(chunks_.size());
    for (const auto& [chunk_key, chunk_data] : chunks_) {
        (void)chunk_data;
        chunk_coords.push_back(ChunkCoord{
            .x = chunk_key.x,
            .y = chunk_key.y,
        });
    }

    std::sort(
        chunk_coords.begin(),
        chunk_coords.end(),
        [](const ChunkCoord& lhs, const ChunkCoord& rhs) {
            if (lhs.x != rhs.x) {
                return lhs.x < rhs.x;
            }
            return lhs.y < rhs.y;
        });
    return chunk_coords;
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
    std::vector<std::uint16_t> tiles(
        static_cast<std::size_t>(kChunkSize * kChunkSize),
        kMaterialAir);
    for (int local_y = 0; local_y < kChunkSize; ++local_y) {
        const int world_y = chunk_coord.y * kChunkSize + local_y;
        for (int local_x = 0; local_x < kChunkSize; ++local_x) {
            const std::size_t index = LocalIndex(local_x, local_y);
            const int world_x = chunk_coord.x * kChunkSize + local_x;
            tiles[index] = GenerateInitialMaterial(world_x, world_y);
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
