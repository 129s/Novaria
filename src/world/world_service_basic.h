#pragma once

#include "world/world_service.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace novaria::world {

class WorldServiceBasic final : public IWorldService {
public:
    static constexpr int kChunkSize = 32;

    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void LoadChunk(const ChunkCoord& chunk_coord) override;
    void UnloadChunk(const ChunkCoord& chunk_coord) override;
    bool ApplyTileMutation(const TileMutation& mutation, std::string& out_error) override;
    bool BuildChunkSnapshot(
        const ChunkCoord& chunk_coord,
        ChunkSnapshot& out_snapshot,
        std::string& out_error) const override;
    bool ApplyChunkSnapshot(const ChunkSnapshot& snapshot, std::string& out_error) override;
    std::vector<ChunkCoord> ConsumeDirtyChunks() override;

    bool IsChunkLoaded(const ChunkCoord& chunk_coord) const;
    std::size_t LoadedChunkCount() const;
    bool TryReadTile(int tile_x, int tile_y, std::uint16_t& out_material_id) const;

private:
    struct ChunkKey final {
        int x = 0;
        int y = 0;

        bool operator==(const ChunkKey& rhs) const {
            return x == rhs.x && y == rhs.y;
        }
    };

    struct ChunkKeyHasher final {
        std::size_t operator()(const ChunkKey& key) const;
    };

    struct ChunkData final {
        std::vector<std::uint16_t> tiles;
        bool dirty = false;
    };

    static int FloorDiv(int value, int divisor);
    static int PositiveMod(int value, int divisor);
    static ChunkCoord WorldToChunkCoord(int tile_x, int tile_y);
    static std::vector<std::uint16_t> BuildInitialChunkTiles(const ChunkCoord& chunk_coord);
    static std::size_t LocalIndex(int local_x, int local_y);
    static ChunkKey ToChunkKey(const ChunkCoord& chunk_coord);

    ChunkData& EnsureChunk(const ChunkCoord& chunk_coord);
    const ChunkData* FindChunk(const ChunkCoord& chunk_coord) const;

    bool initialized_ = false;
    std::unordered_map<ChunkKey, ChunkData, ChunkKeyHasher> chunks_;
    std::unordered_set<ChunkKey, ChunkKeyHasher> dirty_chunk_keys_;
};

}  // namespace novaria::world
