#include "app/player_controller.h"
#include "app/render_scene_builder.h"
#include "core/config.h"
#include "net/net_service_runtime.h"
#include "script/script_host.h"
#include "sim/simulation_kernel.h"
#include "world/world_service_basic.h"

#include <cstdint>
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

class IssueE2EScriptHost final : public novaria::script::IScriptHost {
public:
    bool Initialize(std::string& out_error) override {
        out_error.clear();
        return true;
    }

    void Shutdown() override {}

    void Tick(const novaria::sim::TickContext& tick_context) override {
        (void)tick_context;
    }

    void DispatchEvent(const novaria::script::ScriptEvent& event_data) override {
        (void)event_data;
    }

    novaria::script::ScriptRuntimeDescriptor RuntimeDescriptor() const override {
        return novaria::script::ScriptRuntimeDescriptor{
            .backend_name = "issue_e2e_fake",
            .api_version = novaria::script::kScriptApiVersion,
            .sandbox_enabled = false,
        };
    }
};

void ClearEdgeTriggers(novaria::app::PlayerInputIntent& input) {
    input.jump_pressed = false;
    input.interaction_primary_pressed = false;
    input.ui_inventory_toggle_pressed = false;
    input.hotbar_select_slot_1 = false;
    input.hotbar_select_slot_2 = false;
    input.hotbar_select_slot_3 = false;
    input.hotbar_select_slot_4 = false;
    input.hotbar_select_slot_5 = false;
    input.hotbar_select_slot_6 = false;
    input.hotbar_select_slot_7 = false;
    input.hotbar_select_slot_8 = false;
    input.hotbar_select_slot_9 = false;
    input.hotbar_select_slot_10 = false;
    input.hotbar_cycle_prev = false;
    input.hotbar_cycle_next = false;
    input.hotbar_select_next_row = false;
    input.smart_mode_toggle_pressed = false;
}

void TickOnce(
    novaria::app::PlayerController& controller,
    const novaria::app::PlayerInputIntent& input,
    novaria::world::WorldServiceBasic& world,
    novaria::sim::SimulationKernel& kernel) {
    controller.Update(input, world, kernel, 1);
    kernel.Update(1.0 / 60.0);
}

void TickRepeat(
    novaria::app::PlayerController& controller,
    novaria::app::PlayerInputIntent input,
    novaria::world::WorldServiceBasic& world,
    novaria::sim::SimulationKernel& kernel,
    int ticks) {
    for (int index = 0; index < ticks; ++index) {
        TickOnce(controller, input, world, kernel);
        ClearEdgeTriggers(input);
    }
}

void AimAtTile(
    const novaria::app::LocalPlayerState& state,
    int target_tile_x,
    int target_tile_y,
    novaria::app::PlayerInputIntent& input) {
    constexpr int kTilePixelSize = 32;
    input.cursor_valid = true;
    input.viewport_width = 640;
    input.viewport_height = 480;
    const int view_tiles_x = input.viewport_width / kTilePixelSize;
    const int view_tiles_y = input.viewport_height / kTilePixelSize;
    const int first_world_tile_x = state.tile_x - view_tiles_x / 2;
    const int first_world_tile_y = state.tile_y - view_tiles_y / 2;
    input.cursor_screen_x = (target_tile_x - first_world_tile_x) * kTilePixelSize;
    input.cursor_screen_y = (target_tile_y - first_world_tile_y) * kTilePixelSize;
}

bool BuildKernel(
    novaria::world::WorldServiceBasic& world,
    novaria::net::NetServiceRuntime& net,
    IssueE2EScriptHost& script,
    novaria::sim::SimulationKernel& kernel) {
    (void)script;
    std::string error;
    net.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    net.ConfigureUdpBackend(0, {.host = "127.0.0.1", .port = 0});
    if (!kernel.Initialize(error)) {
        std::cerr << "[FAIL] kernel initialize failed: " << error << '\n';
        return false;
    }
    for (int chunk_y = -1; chunk_y <= 1; ++chunk_y) {
        for (int chunk_x = -1; chunk_x <= 1; ++chunk_x) {
            world.LoadChunk({.x = chunk_x, .y = chunk_y});
        }
    }
    return true;
}

void GrantPickupMaterial(
    novaria::app::PlayerController& controller,
    novaria::world::WorldServiceBasic& world,
    novaria::sim::SimulationKernel& kernel,
    std::uint16_t material_id,
    std::uint32_t amount) {
    const novaria::app::LocalPlayerState state = controller.State();
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kGameplaySpawnDrop),
        .payload = novaria::sim::command::BuildSpawnDropPayload(
            state.tile_x,
            state.tile_y,
            material_id,
            amount),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kGameplayPickupProbe),
        .payload = novaria::sim::command::BuildPickupProbePayload(state.tile_x, state.tile_y),
    });
    kernel.Update(1.0 / 60.0);
    TickOnce(controller, novaria::app::PlayerInputIntent{}, world, kernel);
}

bool TryReadLightLevel(
    const novaria::platform::RenderScene& scene,
    int world_tile_x,
    int world_tile_y,
    std::uint8_t& out_light_level) {
    for (const auto& tile : scene.tiles) {
        if (tile.world_tile_x == world_tile_x && tile.world_tile_y == world_tile_y) {
            out_light_level = tile.light_level;
            return true;
        }
    }

    return false;
}

bool TestToolGateDropPickupAndReach() {
    bool passed = true;
    novaria::world::WorldServiceBasic world;
    novaria::net::NetServiceRuntime net;
    IssueE2EScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);
    if (!BuildKernel(world, net, script, kernel)) {
        return false;
    }

    novaria::app::PlayerController controller;
    controller.Reset();

    const novaria::app::LocalPlayerState initial_state = controller.State();
    const int near_tile_x = initial_state.tile_x + 1;
    const int far_tile_x = initial_state.tile_x + 6;
    const int target_tile_y = initial_state.tile_y;
    std::string error;

    passed &= Expect(
        world.ApplyTileMutation(
            {.tile_x = near_tile_x, .tile_y = target_tile_y, .material_id = novaria::world::WorldServiceBasic::kMaterialWood},
            error),
        "Near wood mutation should succeed.");
    passed &= Expect(
        world.ApplyTileMutation(
            {.tile_x = far_tile_x, .tile_y = target_tile_y, .material_id = novaria::world::WorldServiceBasic::kMaterialStone},
            error),
        "Far stone mutation should succeed.");

    novaria::app::PlayerInputIntent mine_wood_with_pickaxe{};
    mine_wood_with_pickaxe.action_primary_held = true;
    TickRepeat(controller, mine_wood_with_pickaxe, world, kernel, 20);

    std::uint16_t near_material = 0;
    passed &= Expect(
        world.TryReadTile(near_tile_x, target_tile_y, near_material) &&
            near_material == novaria::world::WorldServiceBasic::kMaterialWood,
        "Pickaxe should not harvest wood target.");

    novaria::app::PlayerInputIntent select_axe{};
    select_axe.hotbar_select_slot_2 = true;
    TickOnce(controller, select_axe, world, kernel);
    novaria::app::PlayerInputIntent chop_wood{};
    chop_wood.action_primary_held = true;
    TickRepeat(controller, chop_wood, world, kernel, 16);
    passed &= Expect(
        world.TryReadTile(near_tile_x, target_tile_y, near_material) &&
            near_material == novaria::world::WorldServiceBasic::kMaterialAir,
        "Axe should harvest wood target.");
    passed &= Expect(
        controller.State().inventory_wood_count == 0,
        "Harvested wood should not auto-enter inventory before pickup.");

    novaria::app::PlayerInputIntent move_right{};
    move_right.move_right = true;
    TickOnce(controller, move_right, world, kernel);
    TickOnce(controller, novaria::app::PlayerInputIntent{}, world, kernel);
    passed &= Expect(
        controller.State().inventory_wood_count >= 1,
        "Move contact should resolve world drop pickup.");

    novaria::app::PlayerInputIntent far_mine{};
    far_mine.action_primary_held = true;
    AimAtTile(controller.State(), far_tile_x, target_tile_y, far_mine);
    TickRepeat(controller, far_mine, world, kernel, 30);

    std::uint16_t far_material = 0;
    passed &= Expect(
        world.TryReadTile(far_tile_x, target_tile_y, far_material) &&
            far_material == novaria::world::WorldServiceBasic::kMaterialStone,
        "Out-of-reach target should remain unmined.");

    kernel.Shutdown();
    return passed;
}

bool TestWorkbenchReachGateForSwordRecipe() {
    bool passed = true;
    novaria::world::WorldServiceBasic world;
    novaria::net::NetServiceRuntime net;
    IssueE2EScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);
    if (!BuildKernel(world, net, script, kernel)) {
        return false;
    }

    novaria::app::PlayerController controller;
    controller.Reset();
    std::string error;

    GrantPickupMaterial(
        controller,
        world,
        kernel,
        novaria::world::WorldServiceBasic::kMaterialWood,
        8);

    passed &= Expect(
        controller.State().inventory_wood_count >= 7,
        "Recipe reach test should gather enough wood.");

    const novaria::app::LocalPlayerState recipe_state = controller.State();
    const int far_workbench_x = recipe_state.tile_x + 6;
    const int near_workbench_x = recipe_state.tile_x + 1;
    const int workbench_y = recipe_state.tile_y;
    passed &= Expect(
        world.ApplyTileMutation(
            {.tile_x = far_workbench_x, .tile_y = workbench_y, .material_id = novaria::world::WorldServiceBasic::kMaterialWorkbench},
            error),
        "Far workbench mutation should succeed.");

    novaria::app::PlayerInputIntent open_inventory{};
    open_inventory.ui_inventory_toggle_pressed = true;
    TickOnce(controller, open_inventory, world, kernel);
    novaria::app::PlayerInputIntent select_sword_recipe{};
    select_sword_recipe.hotbar_select_slot_2 = true;
    TickOnce(controller, select_sword_recipe, world, kernel);
    novaria::app::PlayerInputIntent craft_sword_far{};
    craft_sword_far.interaction_primary_pressed = true;
    TickOnce(controller, craft_sword_far, world, kernel);
    passed &= Expect(
        controller.State().inventory_wood_sword_count == 0,
        "Sword recipe should fail when workbench is out of reach.");

    passed &= Expect(
        world.ApplyTileMutation(
            {.tile_x = near_workbench_x, .tile_y = workbench_y, .material_id = novaria::world::WorldServiceBasic::kMaterialWorkbench},
            error),
        "Near workbench mutation should succeed.");
    novaria::app::PlayerInputIntent craft_sword_near{};
    craft_sword_near.interaction_primary_pressed = true;
    TickOnce(controller, craft_sword_near, world, kernel);
    passed &= Expect(
        controller.State().inventory_wood_sword_count >= 1,
        "Sword recipe should pass when workbench is reachable.");

    kernel.Shutdown();
    return passed;
}

bool TestTorchCraftPlaceAndLighting() {
    bool passed = true;
    novaria::world::WorldServiceBasic world;
    novaria::net::NetServiceRuntime net;
    IssueE2EScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);
    if (!BuildKernel(world, net, script, kernel)) {
        return false;
    }

    novaria::app::PlayerController controller;
    controller.Reset();
    std::string error;

    GrantPickupMaterial(
        controller,
        world,
        kernel,
        novaria::world::WorldServiceBasic::kMaterialWood,
        2);
    GrantPickupMaterial(
        controller,
        world,
        kernel,
        novaria::world::WorldServiceBasic::kMaterialCoalOre,
        1);

    novaria::app::LocalPlayerState state = controller.State();
    int target_x = state.tile_x + 1;
    int target_y = state.tile_y;

    passed &= Expect(
        controller.State().inventory_wood_count >= 1 &&
            controller.State().inventory_coal_count >= 1,
        "Torch recipe materials should be available.");

    novaria::app::PlayerInputIntent open_inventory{};
    open_inventory.ui_inventory_toggle_pressed = true;
    TickOnce(controller, open_inventory, world, kernel);
    novaria::app::PlayerInputIntent select_torch_recipe{};
    select_torch_recipe.hotbar_select_slot_3 = true;
    TickOnce(controller, select_torch_recipe, world, kernel);
    novaria::app::PlayerInputIntent craft_torch{};
    craft_torch.interaction_primary_pressed = true;
    TickOnce(controller, craft_torch, world, kernel);
    passed &= Expect(
        controller.State().inventory_torch_count >= 4,
        "Torch recipe should produce torches.");

    TickOnce(controller, open_inventory, world, kernel);
    novaria::app::PlayerInputIntent select_torch_slot{};
    select_torch_slot.hotbar_select_slot_5 = true;
    TickOnce(controller, select_torch_slot, world, kernel);
    state = controller.State();
    target_x = state.tile_x + 1;
    target_y = state.tile_y;
    passed &= Expect(
        world.ApplyTileMutation(
            {.tile_x = target_x, .tile_y = target_y, .material_id = novaria::world::WorldServiceBasic::kMaterialAir},
            error),
        "Torch placement target should be forced to air.");
    novaria::app::PlayerInputIntent place_torch{};
    place_torch.action_primary_held = true;
    TickRepeat(controller, place_torch, world, kernel, 10);

    std::uint16_t placed_material = 0;
    passed &= Expect(
        world.TryReadTile(target_x, target_y, placed_material) &&
            placed_material == novaria::world::WorldServiceBasic::kMaterialTorch,
        "Torch should be placed as world tile.");
    passed &= Expect(
        controller.State().inventory_torch_count <= 3,
        "Torch placement should consume inventory count.");

    novaria::app::RenderSceneBuilder render_scene_builder;
    novaria::core::GameConfig config{};
    config.window_width = 640;
    config.window_height = 480;
    const novaria::platform::RenderScene scene = render_scene_builder.Build(
        controller.State(),
        config,
        world,
        0.0F);
    std::uint8_t torch_light = 0;
    std::uint8_t far_light = 0;
    const bool has_torch_light = TryReadLightLevel(scene, target_x, target_y, torch_light);
    const bool has_far_light = TryReadLightLevel(scene, target_x + 7, target_y, far_light);
    passed &= Expect(
        has_torch_light && has_far_light,
        "Render scene should expose light levels for sampled tiles.");
    if (has_torch_light && has_far_light) {
        passed &= Expect(
            torch_light > far_light,
            "Torch-adjacent tile should be brighter than distant tile at night.");
    }

    kernel.Shutdown();
    return passed;
}

bool TestSmartModeAndHotbarRowCycle() {
    bool passed = true;
    novaria::world::WorldServiceBasic world;
    novaria::net::NetServiceRuntime net;
    IssueE2EScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);
    if (!BuildKernel(world, net, script, kernel)) {
        return false;
    }

    novaria::app::PlayerController controller;
    controller.Reset();
    std::string error;

    const novaria::app::LocalPlayerState start_state = controller.State();
    const int target_x = start_state.tile_x + 1;
    const int target_y = start_state.tile_y;
    passed &= Expect(
        world.ApplyTileMutation(
            {.tile_x = target_x, .tile_y = target_y, .material_id = novaria::world::WorldServiceBasic::kMaterialStone},
            error),
        "Stone mutation for smart-mode test should succeed.");

    novaria::app::PlayerInputIntent tab_row{};
    tab_row.hotbar_select_next_row = true;
    TickOnce(controller, tab_row, world, kernel);
    passed &= Expect(
        controller.State().active_hotbar_row == 1,
        "Tab should cycle active hotbar row forward.");
    TickOnce(controller, tab_row, world, kernel);
    passed &= Expect(
        controller.State().active_hotbar_row == 0,
        "Hotbar row cycle should wrap around.");

    novaria::app::PlayerInputIntent select_slot_four{};
    select_slot_four.hotbar_select_slot_4 = true;
    TickOnce(controller, select_slot_four, world, kernel);
    passed &= Expect(
        controller.State().selected_hotbar_slot == 3,
        "Slot shortcut should select expected hotbar slot.");

    novaria::app::PlayerInputIntent toggle_smart{};
    toggle_smart.smart_mode_toggle_pressed = true;
    TickOnce(controller, toggle_smart, world, kernel);
    passed &= Expect(
        controller.State().smart_mode_enabled,
        "Ctrl toggle should enable smart mode.");

    novaria::app::PlayerInputIntent hold_shift{};
    hold_shift.smart_context_held = true;
    TickOnce(controller, hold_shift, world, kernel);
    passed &= Expect(
        controller.State().context_slot_visible,
        "Hold Shift should expose context slot.");
    passed &= Expect(
        controller.State().context_slot_current == 0,
        "Smart context should suggest pickaxe slot for stone target.");

    TickOnce(controller, novaria::app::PlayerInputIntent{}, world, kernel);
    passed &= Expect(
        !controller.State().context_slot_visible &&
            controller.State().selected_hotbar_slot == 3,
        "Release Shift should hide context slot and restore previous slot.");

    kernel.Shutdown();
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestToolGateDropPickupAndReach();
    passed &= TestWorkbenchReachGateForSwordRecipe();
    passed &= TestTorchCraftPlaceAndLighting();
    passed &= TestSmartModeAndHotbarRowCycle();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_gameplay_issue_e2e_tests\n";
    return 0;
}
