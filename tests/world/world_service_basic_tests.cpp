#include "world/world_service_basic.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

bool ContainsChunk(
    const std::vector<novaria::world::ChunkCoord>& chunks,
    novaria::world::ChunkCoord target) {
    return std::any_of(chunks.begin(), chunks.end(), [target](const novaria::world::ChunkCoord& chunk) {
        return chunk.x == target.x && chunk.y == target.y;
    });
}

}  // namespace

int main() {
    bool passed = true;
    novaria::world::WorldServiceBasic world_service;
    std::string error;

    passed &= Expect(world_service.Initialize(error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error message.");
    passed &= Expect(world_service.LoadedChunkCount() == 0, "No chunks should be loaded at start.");

    world_service.LoadChunk({.x = 0, .y = 0});
    passed &= Expect(world_service.IsChunkLoaded({.x = 0, .y = 0}), "Chunk (0,0) should be loaded.");
    passed &= Expect(world_service.LoadedChunkCount() == 1, "Loaded chunk count should be one.");

    std::uint16_t material_id = 0;
    passed &= Expect(world_service.TryReadTile(0, 0, material_id), "Tile (0,0) should be readable.");
    passed &= Expect(material_id == 1, "Tile (0,0) should be initial dirt.");

    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = 0, .tile_y = 0, .material_id = 99}, error),
        "Tile mutation at (0,0) should succeed.");
    passed &= Expect(error.empty(), "Mutation at (0,0) should not report error.");
    passed &= Expect(world_service.TryReadTile(0, 0, material_id), "Tile (0,0) should still be readable.");
    passed &= Expect(material_id == 99, "Tile (0,0) should be overwritten by mutation.");
    {
        const auto dirty_chunks = world_service.ConsumeDirtyChunks();
        passed &= Expect(dirty_chunks.size() == 1, "One dirty chunk should be reported after first mutation.");
        passed &= Expect(ContainsChunk(dirty_chunks, {.x = 0, .y = 0}), "Dirty chunk should contain (0,0).");
        passed &= Expect(
            world_service.ConsumeDirtyChunks().empty(),
            "Dirty chunks should be cleared after consume.");
    }

    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = -1, .tile_y = -1, .material_id = 7}, error),
        "Tile mutation at negative coordinate should succeed.");
    passed &= Expect(world_service.IsChunkLoaded({.x = -1, .y = -1}), "Negative chunk should be auto-loaded.");
    passed &= Expect(
        world_service.TryReadTile(-1, -1, material_id) && material_id == 7,
        "Tile (-1,-1) should match mutation.");
    {
        const auto dirty_chunks = world_service.ConsumeDirtyChunks();
        passed &= Expect(ContainsChunk(dirty_chunks, {.x = -1, .y = -1}), "Dirty chunk should contain (-1,-1).");
    }

    world_service.UnloadChunk({.x = 0, .y = 0});
    passed &= Expect(!world_service.IsChunkLoaded({.x = 0, .y = 0}), "Chunk (0,0) should be unloaded.");

    world_service.Shutdown();
    passed &= Expect(
        !world_service.ApplyTileMutation({.tile_x = 2, .tile_y = 2, .material_id = 3}, error),
        "Mutation should fail after shutdown.");
    passed &= Expect(!error.empty(), "Mutation failure after shutdown should provide an error.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_world_service_tests\n";
    return 0;
}
