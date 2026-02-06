#pragma once

#include "sim/tick_context.h"

#include <cstdint>
#include <string>
#include <vector>

namespace novaria::world {

struct ChunkCoord final {
    int x = 0;
    int y = 0;
};

struct TileMutation final {
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
};

struct ChunkSnapshot final {
    ChunkCoord chunk_coord;
    std::vector<std::uint16_t> tiles;
};

class IWorldService {
public:
    virtual ~IWorldService() = default;

    virtual bool Initialize(std::string& out_error) = 0;
    virtual void Shutdown() = 0;
    virtual void Tick(const sim::TickContext& tick_context) = 0;
    virtual void LoadChunk(const ChunkCoord& chunk_coord) = 0;
    virtual void UnloadChunk(const ChunkCoord& chunk_coord) = 0;
    virtual bool ApplyTileMutation(const TileMutation& mutation, std::string& out_error) = 0;
    virtual bool BuildChunkSnapshot(
        const ChunkCoord& chunk_coord,
        ChunkSnapshot& out_snapshot,
        std::string& out_error) const = 0;
    virtual std::vector<ChunkCoord> ConsumeDirtyChunks() = 0;
};

}  // namespace novaria::world
