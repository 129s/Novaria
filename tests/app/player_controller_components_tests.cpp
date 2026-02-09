#include "app/player_controller_components.h"
#include "world/material_catalog.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

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
    struct KeyHash final {
        std::size_t operator()(const std::pair<int, int>& key) const {
            return std::hash<int>{}(key.first) ^ (std::hash<int>{}(key.second) << 1);
        }
    };

    std::unordered_map<std::pair<int, int>, std::uint16_t, KeyHash> tiles_;
};

bool TestResolveTargetAndReachability() {
    novaria::app::LocalPlayerState state{};
    state.position_x = 0.0F;
    state.position_y = 0.0F;
    state.tile_x = 0;
    state.tile_y = 0;
    state.facing_x = 1;

    novaria::app::PlayerInputIntent input{};
    input.cursor_valid = true;
    input.viewport_width = 640;
    input.viewport_height = 480;
    input.cursor_screen_x = (input.viewport_width / 2) + 32;
    input.cursor_screen_y = (input.viewport_height / 2);
    const novaria::app::controller::TargetResolution resolution =
        novaria::app::controller::ResolveTarget(state, input, 32, 4);

    bool passed = true;
    passed &= Expect(
        resolution.tile_x == 1 && resolution.tile_y == 0,
        "Target resolution should map cursor to expected world tile.");
    passed &= Expect(
        resolution.reachable,
        "Resolved near target should be reachable.");

    input.cursor_screen_x = input.viewport_width - 1;
    input.cursor_screen_y = input.viewport_height - 1;
    const novaria::app::controller::TargetResolution far_resolution =
        novaria::app::controller::ResolveTarget(state, input, 32, 4);
    passed &= Expect(
        !far_resolution.reachable,
        "Resolved far target should be unreachable.");

    input.viewport_width = 320;
    input.viewport_height = 240;
    input.cursor_screen_x = 5 * 32;
    input.cursor_screen_y = 3 * 32;
    const novaria::app::controller::TargetResolution resized_resolution =
        novaria::app::controller::ResolveTarget(state, input, 32, 4);
    passed &= Expect(
        resized_resolution.tile_x != resolution.tile_x ||
            resized_resolution.tile_y != resolution.tile_y,
        "Target resolution should respond to resized viewport dimensions.");
    return passed;
}

bool TestChunkWindowController() {
    bool passed = true;
    novaria::app::LocalPlayerState state{};
    state.tile_x = 0;
    state.tile_y = 0;

    std::vector<std::pair<int, int>> loads;
    std::vector<std::pair<int, int>> unloads;
    novaria::app::controller::UpdateChunkWindow(
        state,
        1,
        [&loads](int x, int y) { loads.push_back({x, y}); },
        [&unloads](int x, int y) { unloads.push_back({x, y}); });

    passed &= Expect(
        state.loaded_chunk_window_ready,
        "Chunk window update should set ready state.");
    passed &= Expect(
        loads.size() == 9,
        "Initial chunk window should load 3x3 chunks.");
    passed &= Expect(
        unloads.empty(),
        "Initial chunk window should not unload chunks.");

    state.tile_x = novaria::world::kChunkTileSize;
    loads.clear();
    unloads.clear();
    novaria::app::controller::UpdateChunkWindow(
        state,
        1,
        [&loads](int x, int y) { loads.push_back({x, y}); },
        [&unloads](int x, int y) { unloads.push_back({x, y}); });
    passed &= Expect(
        !loads.empty() && !unloads.empty(),
        "Chunk window shift should load and unload chunk strips.");
    return passed;
}

bool TestHotbarAndSmartSlotComponents() {
    bool passed = true;
    novaria::app::LocalPlayerState state{};
    state.inventory_open = false;
    state.selected_hotbar_slot = 0;
    state.active_hotbar_row = 0;

    std::vector<std::uint8_t> applied_slots;
    novaria::app::PlayerInputIntent hotbar_input{};
    hotbar_input.hotbar_select_slot_4 = true;
    novaria::app::controller::ApplyHotbarInput(
        state,
        hotbar_input,
        2,
        [&applied_slots](std::uint8_t slot) { applied_slots.push_back(slot); });
    passed &= Expect(
        applied_slots.size() == 1 && applied_slots.front() == 3,
        "Hotbar component should route slot shortcut to expected slot index.");

    state.inventory_open = true;
    novaria::app::PlayerInputIntent recipe_input{};
    recipe_input.hotbar_select_slot_2 = true;
    novaria::app::controller::ApplyHotbarInput(
        state,
        recipe_input,
        2,
        [&applied_slots](std::uint8_t slot) { applied_slots.push_back(slot); });
    passed &= Expect(
        state.selected_recipe_index == 1,
        "Inventory-open hotbar input should switch selected recipe.");

    FakeWorldService world;
    std::string error;
    (void)world.Initialize(error);
    world.ApplyTileMutation(
        {.tile_x = 1, .tile_y = 0, .material_id = novaria::world::material::kStone},
        error);
    const std::uint8_t suggested_slot = novaria::app::controller::ResolveSmartContextSlot(
        state,
        world,
        1,
        0);
    passed &= Expect(
        suggested_slot == 0,
        "Smart slot resolver should choose pickaxe slot for stone.");
    return passed;
}

bool TestPrimaryActionPlanResolution() {
    bool passed = true;
    novaria::app::LocalPlayerState state{};
    state.active_hotbar_row = 0;
    state.selected_hotbar_slot = 0;
    state.has_pickaxe_tool = true;

    novaria::app::controller::PrimaryActionPlan plan{};
    passed &= Expect(
        novaria::app::controller::ResolvePrimaryActionPlan(
            state,
            novaria::world::material::kStone,
            8,
            plan) &&
            plan.is_harvest,
        "Pickaxe slot should resolve harvest action for stone.");

    state.selected_hotbar_slot = 2;
    state.inventory_dirt_count = 2;
    plan = {};
    passed &= Expect(
        novaria::app::controller::ResolvePrimaryActionPlan(
            state,
            novaria::world::material::kAir,
            8,
            plan) &&
            plan.is_place &&
            plan.place_material_id == novaria::world::material::kDirt,
        "Dirt slot should resolve place action when inventory is available.");

    state.selected_hotbar_slot = 4;
    state.inventory_torch_count = 0;
    plan = {};
    passed &= Expect(
        !novaria::app::controller::ResolvePrimaryActionPlan(
            state,
            novaria::world::material::kAir,
            8,
            plan),
        "Torch slot should not resolve when no torches are in inventory.");
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestResolveTargetAndReachability();
    passed &= TestChunkWindowController();
    passed &= TestHotbarAndSmartSlotComponents();
    passed &= TestPrimaryActionPlanResolution();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_player_controller_components_tests\n";
    return 0;
}
