#include "world/snapshot_codec.h"
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

bool ReplicateDirtyChunks(
    novaria::world::WorldServiceBasic& source_world,
    novaria::world::WorldServiceBasic& target_world,
    std::string& error) {
    const auto dirty_chunks = source_world.ConsumeDirtyChunks();
    for (const auto& chunk_coord : dirty_chunks) {
        novaria::world::ChunkSnapshot snapshot{};
        if (!source_world.BuildChunkSnapshot(chunk_coord, snapshot, error)) {
            return false;
        }

        std::string payload;
        if (!novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(snapshot, payload, error)) {
            return false;
        }

        novaria::world::ChunkSnapshot decoded{};
        if (!novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot(payload, decoded, error)) {
            return false;
        }

        if (!target_world.ApplyChunkSnapshot(decoded, error)) {
            return false;
        }
    }

    return true;
}

}  // namespace

int main() {
    bool passed = true;
    std::string error;

    novaria::world::WorldServiceBasic source_world;
    novaria::world::WorldServiceBasic target_world;

    passed &= Expect(source_world.Initialize(error), "Source world initialize should succeed.");
    passed &= Expect(target_world.Initialize(error), "Target world initialize should succeed.");

    source_world.LoadChunk({.x = 0, .y = 0});
    passed &= Expect(
        source_world.ApplyTileMutation({.tile_x = 0, .tile_y = 0, .material_id = 77}, error),
        "Source mutation at (0,0) should succeed.");
    passed &= Expect(
        source_world.ApplyTileMutation({.tile_x = -1, .tile_y = -1, .material_id = 88}, error),
        "Source mutation at (-1,-1) should succeed.");

    passed &= Expect(
        ReplicateDirtyChunks(source_world, target_world, error),
        "Dirty chunk replication should succeed.");

    std::uint16_t material_id = 0;
    passed &= Expect(
        target_world.TryReadTile(0, 0, material_id) && material_id == 77,
        "Target tile (0,0) should match source mutation.");
    passed &= Expect(
        target_world.TryReadTile(-1, -1, material_id) && material_id == 88,
        "Target tile (-1,-1) should match source mutation.");

    source_world.Shutdown();
    target_world.Shutdown();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_world_replication_flow_tests\n";
    return 0;
}
