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

    {
        novaria::world::ChunkSnapshot snapshot{};
        passed &= Expect(
            world_service.BuildChunkSnapshot({.x = 0, .y = 0}, snapshot, error),
            "BuildChunkSnapshot should succeed for loaded chunk.");
        passed &= Expect(!snapshot.tiles.empty(), "Chunk snapshot should contain tile data.");

        novaria::world::ChunkSnapshot incoming_snapshot{
            .chunk_coord = {.x = 0, .y = 0},
            .tiles = std::vector<std::uint16_t>(
                static_cast<std::size_t>(novaria::world::WorldServiceBasic::kChunkSize *
                                         novaria::world::WorldServiceBasic::kChunkSize),
                42),
        };
        passed &= Expect(
            world_service.ApplyChunkSnapshot(incoming_snapshot, error),
            "ApplyChunkSnapshot should succeed for valid snapshot.");
        passed &= Expect(
            world_service.TryReadTile(0, 0, material_id) && material_id == 42,
            "Tile should reflect applied snapshot data.");

        incoming_snapshot.tiles = {1, 2, 3};
        passed &= Expect(
            !world_service.ApplyChunkSnapshot(incoming_snapshot, error),
            "ApplyChunkSnapshot should fail for invalid tile count.");
    }

    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = 0, .tile_y = 0, .material_id = 99}, error),
        "Tile mutation at (0,0) should succeed.");
    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = 1, .tile_y = 1, .material_id = 100}, error),
        "Second mutation in same chunk should also succeed.");
    passed &= Expect(error.empty(), "Mutation at (0,0) should not report error.");
    passed &= Expect(world_service.TryReadTile(0, 0, material_id), "Tile (0,0) should still be readable.");
    passed &= Expect(material_id == 99, "Tile (0,0) should be overwritten by mutation.");
    {
        const auto dirty_chunks = world_service.ConsumeDirtyChunks();
        passed &= Expect(
            dirty_chunks.size() == 1,
            "Multiple mutations in same chunk should still report one dirty chunk.");
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

    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = 65, .tile_y = 0, .material_id = 3}, error),
        "Mutation in chunk (2,0) should succeed.");
    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = -33, .tile_y = 0, .material_id = 4}, error),
        "Mutation in chunk (-2,0) should succeed.");
    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = 0, .tile_y = -33, .material_id = 5}, error),
        "Mutation in chunk (0,-2) should succeed.");
    {
        const auto dirty_chunks = world_service.ConsumeDirtyChunks();
        passed &= Expect(dirty_chunks.size() == 3, "Three chunks should be reported dirty.");
        if (dirty_chunks.size() == 3) {
            passed &= Expect(
                dirty_chunks[0].x == -2 && dirty_chunks[0].y == 0,
                "Dirty chunks should be sorted by x then y (entry 0).");
            passed &= Expect(
                dirty_chunks[1].x == 0 && dirty_chunks[1].y == -2,
                "Dirty chunks should be sorted by x then y (entry 1).");
            passed &= Expect(
                dirty_chunks[2].x == 2 && dirty_chunks[2].y == 0,
                "Dirty chunks should be sorted by x then y (entry 2).");
        }
    }

    world_service.UnloadChunk({.x = 0, .y = 0});
    passed &= Expect(!world_service.IsChunkLoaded({.x = 0, .y = 0}), "Chunk (0,0) should be unloaded.");
    {
        novaria::world::ChunkSnapshot snapshot{};
        passed &= Expect(
            !world_service.BuildChunkSnapshot({.x = 0, .y = 0}, snapshot, error),
            "BuildChunkSnapshot should fail for unloaded chunk.");
    }

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
