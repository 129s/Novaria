#include "runtime/world_service_factory.h"
#include "world/material_catalog.h"

#include <algorithm>
#include <iostream>
#include <memory>
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
    std::unique_ptr<novaria::world::IWorldService> world_service =
        novaria::runtime::CreateWorldService();
    passed &= Expect(world_service != nullptr, "World service factory should not return null.");
    std::string error;

    passed &= Expect(world_service->Initialize(error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error message.");
    passed &= Expect(
        world_service->ConsumeDirtyChunks().empty(),
        "No chunks should be dirty at start.");

    world_service->LoadChunk({.x = 0, .y = 0});
    std::uint16_t material_id = 0;
    passed &= Expect(world_service->TryReadTile(0, 0, material_id), "Tile (0,0) should be readable.");
    passed &= Expect(material_id == novaria::world::material::kDirt, "Tile (0,0) should be initial dirt.");

    for (int chunk_y = -1; chunk_y <= 1; ++chunk_y) {
        for (int chunk_x = -3; chunk_x <= 3; ++chunk_x) {
            world_service->LoadChunk({.x = chunk_x, .y = chunk_y});
        }
    }

    std::size_t observed_grass_tile_count = 0;
    std::size_t observed_water_tile_count = 0;
    std::size_t observed_tree_tile_count = 0;
    for (int tile_y = -12; tile_y <= 24; ++tile_y) {
        for (int tile_x = -96; tile_x <= 96; ++tile_x) {
            if (!world_service->TryReadTile(tile_x, tile_y, material_id)) {
                continue;
            }

            if (material_id == novaria::world::material::kGrass) {
                ++observed_grass_tile_count;
            }
            if (material_id == novaria::world::material::kWater) {
                ++observed_water_tile_count;
            }
            if (material_id == novaria::world::material::kWood ||
                material_id == novaria::world::material::kLeaves) {
                ++observed_tree_tile_count;
            }
        }
    }
    passed &= Expect(
        observed_grass_tile_count > 0,
        "Initial terrain should generate at least one grass tile in loaded range.");
    passed &= Expect(
        observed_water_tile_count > 0,
        "Initial terrain should generate at least one static water tile in loaded range.");
    passed &= Expect(
        observed_tree_tile_count > 0,
        "Initial terrain should generate at least one tree tile in loaded range.");

    {
        novaria::world::ChunkSnapshot snapshot{};
        passed &= Expect(
            world_service->BuildChunkSnapshot({.x = 0, .y = 0}, snapshot, error),
            "BuildChunkSnapshot should succeed for loaded chunk.");
        passed &= Expect(!snapshot.tiles.empty(), "Chunk snapshot should contain tile data.");

        novaria::world::ChunkSnapshot incoming_snapshot{
            .chunk_coord = {.x = 0, .y = 0},
            .tiles = std::vector<std::uint16_t>(
                static_cast<std::size_t>(novaria::world::kChunkTileSize *
                                         novaria::world::kChunkTileSize),
                42),
        };
        passed &= Expect(
            world_service->ApplyChunkSnapshot(incoming_snapshot, error),
            "ApplyChunkSnapshot should succeed for valid snapshot.");
        passed &= Expect(
            world_service->TryReadTile(0, 0, material_id) && material_id == 42,
            "Tile should reflect applied snapshot data.");

        incoming_snapshot.tiles = {1, 2, 3};
        passed &= Expect(
            !world_service->ApplyChunkSnapshot(incoming_snapshot, error),
            "ApplyChunkSnapshot should fail for invalid tile count.");
    }

    passed &= Expect(
        world_service->ApplyTileMutation({.tile_x = 0, .tile_y = 0, .material_id = 99}, error),
        "Tile mutation at (0,0) should succeed.");
    passed &= Expect(
        world_service->ApplyTileMutation({.tile_x = 1, .tile_y = 1, .material_id = 100}, error),
        "Second mutation in same chunk should also succeed.");
    passed &= Expect(error.empty(), "Mutation at (0,0) should not report error.");
    passed &= Expect(world_service->TryReadTile(0, 0, material_id), "Tile (0,0) should still be readable.");
    passed &= Expect(material_id == 99, "Tile (0,0) should be overwritten by mutation.");
    {
        const auto dirty_chunks = world_service->ConsumeDirtyChunks();
        passed &= Expect(
            dirty_chunks.size() == 1,
            "Multiple mutations in same chunk should still report one dirty chunk.");
        passed &= Expect(ContainsChunk(dirty_chunks, {.x = 0, .y = 0}), "Dirty chunk should contain (0,0).");
        passed &= Expect(
            world_service->ConsumeDirtyChunks().empty(),
            "Dirty chunks should be cleared after consume.");
    }

    passed &= Expect(
        world_service->ApplyTileMutation({.tile_x = -1, .tile_y = -1, .material_id = 7}, error),
        "Tile mutation at negative coordinate should succeed.");
    passed &= Expect(
        world_service->TryReadTile(-1, -1, material_id),
        "Negative chunk should be auto-loaded.");
    passed &= Expect(
        world_service->TryReadTile(-1, -1, material_id) && material_id == 7,
        "Tile (-1,-1) should match mutation.");
    {
        const auto dirty_chunks = world_service->ConsumeDirtyChunks();
        passed &= Expect(ContainsChunk(dirty_chunks, {.x = -1, .y = -1}), "Dirty chunk should contain (-1,-1).");
    }

    passed &= Expect(
        world_service->ApplyTileMutation({.tile_x = 65, .tile_y = 0, .material_id = 3}, error),
        "Mutation in chunk (2,0) should succeed.");
    passed &= Expect(
        world_service->ApplyTileMutation({.tile_x = -33, .tile_y = 0, .material_id = 4}, error),
        "Mutation in chunk (-2,0) should succeed.");
    passed &= Expect(
        world_service->ApplyTileMutation({.tile_x = 0, .tile_y = -33, .material_id = 5}, error),
        "Mutation in chunk (0,-2) should succeed.");
    {
        const auto dirty_chunks = world_service->ConsumeDirtyChunks();
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

    world_service->UnloadChunk({.x = 0, .y = 0});
    passed &= Expect(
        !world_service->TryReadTile(0, 0, material_id),
        "Tile (0,0) should be unreadable after chunk unload.");
    {
        novaria::world::ChunkSnapshot snapshot{};
        passed &= Expect(
            !world_service->BuildChunkSnapshot({.x = 0, .y = 0}, snapshot, error),
            "BuildChunkSnapshot should fail for unloaded chunk.");
    }

    world_service->Shutdown();
    passed &= Expect(
        !world_service->ApplyTileMutation({.tile_x = 2, .tile_y = 2, .material_id = 3}, error),
        "Mutation should fail after shutdown.");
    passed &= Expect(!error.empty(), "Mutation failure after shutdown should provide an error.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_world_service_tests\n";
    return 0;
}
