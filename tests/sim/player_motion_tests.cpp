#include "sim/player_motion.h"
#include "world/material_catalog.h"

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

class TestWorldService final : public novaria::world::IWorldService {
public:
    void SetLoadedBounds(int min_tile_x, int max_tile_x, int min_tile_y, int max_tile_y) {
        bounds_enabled_ = true;
        min_tile_x_ = min_tile_x;
        max_tile_x_ = max_tile_x;
        min_tile_y_ = min_tile_y;
        max_tile_y_ = max_tile_y;
    }

    void SetSolidTile(int tile_x, int tile_y, std::uint16_t material_id) {
        tiles_[{tile_x, tile_y}] = material_id;
    }

    bool Initialize(std::string& out_error) override {
        out_error.clear();
        return true;
    }
    void Shutdown() override {}
<<<<<<< HEAD
    void Tick(const novaria::core::TickContext& tick_context) override { (void)tick_context; }
=======
    void Tick(const novaria::sim::TickContext& tick_context) override { (void)tick_context; }
>>>>>>> 77c2e72a388234fbfa90639e804362c787d0e052
    void LoadChunk(const novaria::world::ChunkCoord& chunk_coord) override { (void)chunk_coord; }
    void UnloadChunk(const novaria::world::ChunkCoord& chunk_coord) override { (void)chunk_coord; }
    bool ApplyTileMutation(const novaria::world::TileMutation& mutation, std::string& out_error) override {
        SetSolidTile(mutation.tile_x, mutation.tile_y, mutation.material_id);
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
    bool TryReadTile(int tile_x, int tile_y, std::uint16_t& out_material_id) const override {
        if (bounds_enabled_) {
            if (tile_x < min_tile_x_ || tile_x > max_tile_x_ ||
                tile_y < min_tile_y_ || tile_y > max_tile_y_) {
                return false;
            }
        }
        const auto it = tiles_.find({tile_x, tile_y});
        if (it == tiles_.end()) {
            out_material_id = novaria::world::material::kAir;
            return true;
        }
        out_material_id = it->second;
        return true;
    }
    std::vector<novaria::world::ChunkCoord> LoadedChunkCoords() const override { return {}; }
    std::vector<novaria::world::ChunkCoord> ConsumeDirtyChunks() override { return {}; }

private:
    struct PairHash final {
        std::size_t operator()(const std::pair<int, int>& key) const {
            return std::hash<int>{}(key.first) ^ (std::hash<int>{}(key.second) << 1);
        }
    };

    std::unordered_map<std::pair<int, int>, std::uint16_t, PairHash> tiles_;
    bool bounds_enabled_ = false;
    int min_tile_x_ = 0;
    int max_tile_x_ = 0;
    int min_tile_y_ = 0;
    int max_tile_y_ = 0;
};

bool TestJumpIntoWallDoesNotHover() {
    bool passed = true;
    TestWorldService world;
    std::string error;
    (void)world.Initialize(error);

    constexpr int kGroundY = 10;
    world.SetLoadedBounds(-64, 64, -64, kGroundY + 64);
    for (int x = -16; x <= 16; ++x) {
        for (int y = kGroundY; y <= kGroundY + 32; ++y) {
            world.SetSolidTile(x, y, novaria::world::material::kStone);
        }
    }

    constexpr int kWallX = 2;
    for (int y = 6; y <= 9; ++y) {
        world.SetSolidTile(kWallX, y, novaria::world::material::kWood);
    }

    const novaria::sim::PlayerMotionSettings& settings = novaria::sim::DefaultPlayerMotionSettings();
    novaria::sim::PlayerMotionState state{};
    state.position_x = 1.5F;
    state.position_y = static_cast<float>(kGroundY);
    state.on_ground = true;

    novaria::sim::PlayerMotionInput input{};
    input.move_axis = 1.0F;
    input.jump_pressed = true;
    novaria::sim::UpdatePlayerMotion(input, settings, world, 1.0 / 60.0, state);

    bool observed_illegal_hover = false;
    for (int tick = 0; tick < 240; ++tick) {
        input.jump_pressed = false;
        input.move_axis = 1.0F;
        novaria::sim::UpdatePlayerMotion(input, settings, world, 1.0 / 60.0, state);

        if (state.on_ground && state.position_y < static_cast<float>(kGroundY) - 0.5F) {
            observed_illegal_hover = true;
            break;
        }
    }

    passed &= Expect(
        !observed_illegal_hover,
        "Jumping into a wall should not set on_ground on an unsupported ledge.");
    passed &= Expect(
        state.position_y >= static_cast<float>(kGroundY) - 0.01F &&
            state.position_y <= static_cast<float>(kGroundY) + 0.01F,
        "After wall collision and gravity, player should settle back on ground.");
    return passed;
}

bool TestJumpFromGapDoesNotClipIntoCeiling() {
    bool passed = true;
    TestWorldService world;
    std::string error;
    (void)world.Initialize(error);

    constexpr int kGroundY = 10;
    world.SetLoadedBounds(-64, 64, -64, kGroundY + 64);
    for (int x = -16; x <= 16; ++x) {
        for (int y = kGroundY; y <= kGroundY + 32; ++y) {
            world.SetSolidTile(x, y, novaria::world::material::kStone);
        }
    }

    constexpr int kWallX = 2;
    world.SetSolidTile(kWallX, 8, novaria::world::material::kWood);  // ceiling block above gap
    world.SetSolidTile(kWallX, 10, novaria::world::material::kWood); // lower block

    const novaria::sim::PlayerMotionSettings& settings = novaria::sim::DefaultPlayerMotionSettings();
    novaria::sim::PlayerMotionState state{};
    state.position_x = 1.75F;  // half-body peeking into the gap
    state.position_y = static_cast<float>(kGroundY);
    state.on_ground = true;

    float min_feet_y = state.position_y;
    novaria::sim::PlayerMotionInput input{};
    for (int tick = 0; tick < 180; ++tick) {
        input.move_axis = 0.0F;
        input.jump_pressed = (tick == 0);
        novaria::sim::UpdatePlayerMotion(input, settings, world, 1.0 / 60.0, state);
        min_feet_y = std::min(min_feet_y, state.position_y);
    }

    const float min_allowed_feet_y = 9.0F + settings.height;
    passed &= Expect(
        min_feet_y >= (min_allowed_feet_y - 0.01F),
        "Jumping while peeking out of a gap should not clip into the ceiling block.");
    passed &= Expect(
        std::fabs(state.position_y - static_cast<float>(kGroundY)) <= 0.05F,
        "After jump resolution, player should settle back on ground.");
    return passed;
}

bool TestGroundSnapAcquiresFloorWithoutReachingInTick() {
    bool passed = true;
    TestWorldService world;
    std::string error;
    (void)world.Initialize(error);

    constexpr int kGroundY = 10;
    world.SetLoadedBounds(-64, 64, -64, kGroundY + 64);
    for (int x = -16; x <= 16; ++x) {
        for (int y = kGroundY; y <= kGroundY + 32; ++y) {
            world.SetSolidTile(x, y, novaria::world::material::kStone);
        }
    }

    const novaria::sim::PlayerMotionSettings& settings = novaria::sim::DefaultPlayerMotionSettings();
    novaria::sim::PlayerMotionState state{};
    state.position_x = 0.5F;
    state.position_y = static_cast<float>(kGroundY) - 0.04F;
    state.velocity_y = 0.0F;
    state.on_ground = false;

    novaria::sim::PlayerMotionInput input{};
    novaria::sim::UpdatePlayerMotion(input, settings, world, 1.0 / 60.0, state);

    passed &= Expect(
        state.on_ground,
        "Ground snap should acquire floor even when delta_y is smaller than gap-to-floor.");
    passed &= Expect(
        std::fabs(state.position_y - static_cast<float>(kGroundY)) <= 0.01F,
        "Ground snap should place feet on the detected floor surface.");
    return passed;
}

bool TestJumpFromOneTilePitAfterPushing() {
    bool passed = true;
    TestWorldService world;
    std::string error;
    (void)world.Initialize(error);

    constexpr int kGroundY = 10;
    world.SetLoadedBounds(-64, 64, -64, kGroundY + 128);
    for (int x = -16; x <= 16; ++x) {
        for (int y = kGroundY; y <= kGroundY + 64; ++y) {
            world.SetSolidTile(x, y, novaria::world::material::kStone);
        }
    }

    world.SetSolidTile(0, kGroundY, novaria::world::material::kAir); // carve a one-tile pit

    const novaria::sim::PlayerMotionSettings& settings = novaria::sim::DefaultPlayerMotionSettings();
    novaria::sim::PlayerMotionState state{};
    state.position_x = 0.5F;
    state.position_y = static_cast<float>(kGroundY + 1);
    state.on_ground = true;

    novaria::sim::PlayerMotionInput input{};
    for (int tick = 0; tick < 180; ++tick) {
        input.move_axis = 1.0F;
        input.jump_pressed = false;
        novaria::sim::UpdatePlayerMotion(input, settings, world, 1.0 / 60.0, state);
    }

    const float before_jump_y = state.position_y;
    const float before_jump_x = state.position_x;
    const bool before_jump_on_ground = state.on_ground;
    const float before_jump_vy = state.velocity_y;
    input.move_axis = 1.0F;
    input.jump_pressed = true;
    novaria::sim::UpdatePlayerMotion(input, settings, world, 1.0 / 60.0, state);

    bool moved_up = false;
    for (int tick = 0; tick < 30; ++tick) {
        input.jump_pressed = false;
        novaria::sim::UpdatePlayerMotion(input, settings, world, 1.0 / 60.0, state);
        if (state.position_y < before_jump_y - 0.05F) {
            moved_up = true;
            break;
        }
    }

    if (!moved_up) {
        std::cerr
            << "[DIAG] pit_jump: before_jump x=" << before_jump_x
            << " y=" << before_jump_y
            << " vy=" << before_jump_vy
            << " on_ground=" << (before_jump_on_ground ? "true" : "false")
            << " | after x=" << state.position_x
            << " y=" << state.position_y
            << " vy=" << state.velocity_y
            << " on_ground=" << (state.on_ground ? "true" : "false")
            << '\n';
    }

    passed &= Expect(
        moved_up,
        "Pushing against pit wall should not prevent jump from standing floor.");
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestJumpIntoWallDoesNotHover();
    passed &= TestJumpFromGapDoesNotClipIntoCeiling();
    passed &= TestGroundSnapAcquiresFloorWithoutReachingInTick();
    passed &= TestJumpFromOneTilePitAfterPushing();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_player_motion_tests\n";
    return 0;
}
