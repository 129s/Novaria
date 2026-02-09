#include "app/render_scene_builder.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

class FakeWorldService final : public novaria::world::IWorldService {
public:
    bool Initialize(std::string& out_error) override {
        out_error.clear();
        return true;
    }

    void Shutdown() override {}

    void Tick(const novaria::core::TickContext& tick_context) override {
        (void)tick_context;
    }

    void LoadChunk(const novaria::world::ChunkCoord& chunk_coord) override {
        (void)chunk_coord;
    }

    void UnloadChunk(const novaria::world::ChunkCoord& chunk_coord) override {
        (void)chunk_coord;
    }

    bool ApplyTileMutation(const novaria::world::TileMutation& mutation, std::string& out_error) override {
        tiles_[{mutation.tile_x, mutation.tile_y}] = mutation.material_id;
        out_error.clear();
        return true;
    }

    bool BuildChunkSnapshot(
        const novaria::world::ChunkCoord& chunk_coord,
        novaria::world::ChunkSnapshot& out_snapshot,
        std::string& out_error) const override {
        (void)chunk_coord;
        (void)out_snapshot;
        out_error = "not implemented";
        return false;
    }

    bool ApplyChunkSnapshot(const novaria::world::ChunkSnapshot& snapshot, std::string& out_error) override {
        (void)snapshot;
        out_error.clear();
        return true;
    }

    bool TryReadTile(
        int tile_x,
        int tile_y,
        std::uint16_t& out_material_id) const override {
        const auto it = tiles_.find({tile_x, tile_y});
        if (it == tiles_.end()) {
            out_material_id = 0;
            return false;
        }
        out_material_id = it->second;
        return true;
    }

    std::vector<novaria::world::ChunkCoord> LoadedChunkCoords() const override {
        return {};
    }

    std::vector<novaria::world::ChunkCoord> ConsumeDirtyChunks() override {
        return {};
    }

private:
    struct PairHash final {
        std::size_t operator()(const std::pair<int, int>& key) const {
            return std::hash<int>{}(key.first) ^ (std::hash<int>{}(key.second) << 1);
        }
    };

    std::unordered_map<std::pair<int, int>, std::uint16_t, PairHash> tiles_;
};

}  // namespace

int main() {
    bool passed = true;

    novaria::app::LocalPlayerState player_state{};
    player_state.position_x = 32.0F;
    player_state.position_y = 18.0F;
    player_state.tile_x = 32;
    player_state.tile_y = 18;

    FakeWorldService world_service;
    std::string error;
    (void)world_service.Initialize(error);
    world_service.ApplyTileMutation(
        {.tile_x = 32, .tile_y = 18, .material_id = 1},
        error);

    novaria::app::RenderSceneBuilder builder;
    const novaria::platform::RenderScene scene_640x480 =
        builder.Build(player_state, 640, 480, world_service, 1.0F);
    const novaria::platform::RenderScene scene_960x480 =
        builder.Build(player_state, 960, 480, world_service, 1.0F);

    passed &= Expect(
        scene_640x480.view_tiles_x == 22 && scene_640x480.view_tiles_y == 17,
        "Render scene should derive tile viewport from 640x480.");
    passed &= Expect(
        scene_960x480.view_tiles_x == 32 && scene_960x480.view_tiles_y == 17,
        "Render scene should derive tile viewport from 960x480.");
    passed &= Expect(
        scene_640x480.tiles.size() ==
            static_cast<std::size_t>(scene_640x480.view_tiles_x * scene_640x480.view_tiles_y),
        "Render tile count should match derived viewport for 640x480.");
    passed &= Expect(
        scene_960x480.tiles.size() ==
            static_cast<std::size_t>(scene_960x480.view_tiles_x * scene_960x480.view_tiles_y),
        "Render tile count should match derived viewport for 960x480.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_render_scene_builder_tests\n";
    return 0;
}
