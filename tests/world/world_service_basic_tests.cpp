#include "world/world_service_basic.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
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

    passed &= Expect(
        world_service.ApplyTileMutation({.tile_x = -1, .tile_y = -1, .material_id = 7}, error),
        "Tile mutation at negative coordinate should succeed.");
    passed &= Expect(world_service.IsChunkLoaded({.x = -1, .y = -1}), "Negative chunk should be auto-loaded.");
    passed &= Expect(
        world_service.TryReadTile(-1, -1, material_id) && material_id == 7,
        "Tile (-1,-1) should match mutation.");

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
