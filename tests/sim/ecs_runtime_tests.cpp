#include "sim/ecs_runtime.h"
#include "world/material_catalog.h"
#include "world/world_service.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

class EmptyWorldService final : public novaria::world::IWorldService {
public:
    bool Initialize(std::string& out_error) override {
        out_error.clear();
        return true;
    }

    void Shutdown() override {}

<<<<<<< HEAD
    void Tick(const novaria::core::TickContext& tick_context) override {
=======
    void Tick(const novaria::sim::TickContext& tick_context) override {
>>>>>>> 77c2e72a388234fbfa90639e804362c787d0e052
        (void)tick_context;
    }

    void LoadChunk(const novaria::world::ChunkCoord& chunk_coord) override {
        (void)chunk_coord;
    }

    void UnloadChunk(const novaria::world::ChunkCoord& chunk_coord) override {
        (void)chunk_coord;
    }

    bool ApplyTileMutation(const novaria::world::TileMutation& mutation, std::string& out_error) override {
        (void)mutation;
        out_error.clear();
        return true;
    }

    bool BuildChunkSnapshot(
        const novaria::world::ChunkCoord& chunk_coord,
        novaria::world::ChunkSnapshot& out_snapshot,
        std::string& out_error) const override {
        (void)chunk_coord;
        out_snapshot = {};
        out_error = "EmptyWorldService does not support snapshots.";
        return false;
    }

    bool ApplyChunkSnapshot(
        const novaria::world::ChunkSnapshot& snapshot,
        std::string& out_error) override {
        (void)snapshot;
        out_error.clear();
        return true;
    }

    bool TryReadTile(int tile_x, int tile_y, std::uint16_t& out_material_id) const override {
        (void)tile_x;
        (void)tile_y;
        out_material_id = novaria::world::material::kAir;
        return true;
    }

    std::vector<novaria::world::ChunkCoord> LoadedChunkCoords() const override {
        return {};
    }

    std::vector<novaria::world::ChunkCoord> ConsumeDirtyChunks() override {
        return {};
    }
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

bool TestProjectilePipelineCompletesKillFlow() {
    bool passed = true;

    novaria::sim::ecs::Runtime runtime;
    EmptyWorldService world;
    std::string error;
    passed &= Expect(runtime.Initialize(error), "ECS runtime should initialize.");

    const novaria::sim::command::FireProjectilePayload payload{
        .origin_tile_x = 1,
        .origin_tile_y = -4,
        .velocity_milli_x = 4500,
        .velocity_milli_y = 0,
        .damage = 13,
        .lifetime_ticks = 180,
        .faction = 1,
    };

    runtime.QueueSpawnProjectile(7, payload);
    runtime.QueueSpawnProjectile(7, payload);

    for (std::uint64_t tick_index = 0; tick_index < 240; ++tick_index) {
        runtime.Tick({
            .tick_index = tick_index,
            .fixed_delta_seconds = 1.0 / 60.0,
        }, world);
    }

    std::uint16_t total_reward_kills = 0;
    const std::vector<novaria::sim::ecs::CombatEvent> events =
        runtime.ConsumeCombatEvents();
    for (const auto& event_data : events) {
        if (event_data.type == novaria::sim::ecs::CombatEventType::HostileDefeated) {
            total_reward_kills += event_data.reward_kill_count;
        }
    }

    const novaria::sim::ecs::RuntimeDiagnostics diagnostics =
        runtime.DiagnosticsSnapshot();
    passed &= Expect(
        diagnostics.total_projectile_spawned == 2,
        "Two projectiles should be spawned.");
    passed &= Expect(
        diagnostics.total_damage_instances >= 2,
        "Projectile collisions should produce damage instances.");
    passed &= Expect(
        diagnostics.total_hostile_defeated >= 1,
        "At least one hostile should be defeated.");
    passed &= Expect(
        total_reward_kills >= 1,
        "Combat event stream should report hostile defeat.");
    passed &= Expect(
        diagnostics.total_projectile_recycled >= 2,
        "Projectile lifecycle should recycle expired/consumed entities.");

    runtime.Shutdown();
    return passed;
}

bool TestProjectileLifetimeRecycleWithoutCollision() {
    bool passed = true;

    novaria::sim::ecs::Runtime runtime;
    EmptyWorldService world;
    std::string error;
    passed &= Expect(runtime.Initialize(error), "ECS runtime should initialize.");

    runtime.QueueSpawnProjectile(
        1,
        novaria::sim::command::FireProjectilePayload{
            .origin_tile_x = -20,
            .origin_tile_y = -20,
            .velocity_milli_x = 0,
            .velocity_milli_y = 0,
            .damage = 5,
            .lifetime_ticks = 2,
            .faction = 1,
        });

    for (std::uint64_t tick_index = 0; tick_index < 5; ++tick_index) {
        runtime.Tick({
            .tick_index = tick_index,
            .fixed_delta_seconds = 1.0 / 60.0,
        }, world);
    }

    const novaria::sim::ecs::RuntimeDiagnostics diagnostics =
        runtime.DiagnosticsSnapshot();
    passed &= Expect(
        diagnostics.active_projectile_count == 0,
        "Expired projectile should be recycled.");
    passed &= Expect(
        diagnostics.total_projectile_recycled >= 1,
        "Lifetime system should recycle non-colliding projectile.");

    runtime.Shutdown();
    return passed;
}

bool TestDropSpawnAndPickupProbeProducesGameplayEvent() {
    bool passed = true;

    novaria::sim::ecs::Runtime runtime;
    EmptyWorldService world;
    std::string error;
    passed &= Expect(runtime.Initialize(error), "ECS runtime should initialize.");

    runtime.QueueSpawnWorldDrop(novaria::sim::command::SpawnDropPayload{
        .tile_x = 2,
        .tile_y = -3,
        .material_id = 2,
        .amount = 2,
    });
    runtime.Tick({
        .tick_index = 0,
        .fixed_delta_seconds = 1.0 / 60.0,
    }, world);

    runtime.QueuePickupProbe(
        42,
        novaria::sim::command::PickupProbePayload{
            .tile_x = 2,
            .tile_y = -3,
        });
    runtime.Tick({
        .tick_index = 1,
        .fixed_delta_seconds = 1.0 / 60.0,
    }, world);

    const std::vector<novaria::sim::ecs::GameplayEvent> gameplay_events =
        runtime.ConsumeGameplayEvents();
    passed &= Expect(
        gameplay_events.size() == 1,
        "Pickup probe should produce one gameplay event.");
    if (gameplay_events.size() == 1) {
        const auto& pickup_event = gameplay_events.front();
        passed &= Expect(
            pickup_event.type == novaria::sim::ecs::GameplayEventType::PickupResolved,
            "Pickup gameplay event should use PickupResolved type.");
        passed &= Expect(
            pickup_event.player_id == 42 &&
                pickup_event.material_id == 2 &&
                pickup_event.amount == 2 &&
                pickup_event.tile_x == 2 &&
                pickup_event.tile_y == -3,
            "Pickup gameplay event payload should match drop and probe inputs.");
    }

    const novaria::sim::ecs::RuntimeDiagnostics diagnostics =
        runtime.DiagnosticsSnapshot();
    passed &= Expect(
        diagnostics.total_drop_spawned == 2,
        "Drop spawn diagnostics should accumulate spawned amount.");
    passed &= Expect(
        diagnostics.total_drop_picked_up == 2,
        "Drop pickup diagnostics should accumulate picked-up amount.");
    passed &= Expect(
        diagnostics.active_drop_count == 0,
        "Picked drop entity should be removed from ECS registry.");

    runtime.Shutdown();
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestProjectilePipelineCompletesKillFlow();
    passed &= TestProjectileLifetimeRecycleWithoutCollision();
    passed &= TestDropSpawnAndPickupProbeProducesGameplayEvent();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_ecs_runtime_tests\n";
    return 0;
}
