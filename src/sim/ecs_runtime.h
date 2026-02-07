#pragma once

#include "sim/command_schema.h"
#include "sim/tick_context.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace novaria::sim::ecs {

struct Transform final {
    float tile_x = 0.0F;
    float tile_y = 0.0F;
};

struct Velocity final {
    float tile_per_second_x = 0.0F;
    float tile_per_second_y = 0.0F;
};

struct Collider final {
    float radius = 0.5F;
};

struct Health final {
    std::int32_t value = 0;
};

struct Lifetime final {
    std::uint16_t ticks_remaining = 0;
};

struct Faction final {
    std::uint16_t id = 0;
};

struct Projectile final {
    std::uint32_t owner_player_id = 0;
    std::uint16_t damage = 0;
};

struct WorldDrop final {
    std::uint16_t material_id = 0;
    std::uint32_t amount = 0;
};

struct HostileTarget final {
    std::uint16_t reward_kill_count = 1;
};

enum class CombatEventType : std::uint8_t {
    HostileDefeated = 0,
};

struct CombatEvent final {
    CombatEventType type = CombatEventType::HostileDefeated;
    std::uint16_t reward_kill_count = 0;
};

enum class GameplayEventType : std::uint8_t {
    PickupResolved = 0,
};

struct GameplayEvent final {
    GameplayEventType type = GameplayEventType::PickupResolved;
    std::uint32_t player_id = 0;
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    std::uint32_t amount = 0;
};

struct RuntimeDiagnostics final {
    std::size_t active_projectile_count = 0;
    std::size_t active_hostile_count = 0;
    std::size_t active_drop_count = 0;
    std::uint64_t total_projectile_spawned = 0;
    std::uint64_t total_projectile_recycled = 0;
    std::uint64_t total_damage_instances = 0;
    std::uint64_t total_hostile_defeated = 0;
    std::uint64_t total_drop_spawned = 0;
    std::uint64_t total_drop_picked_up = 0;
};

class Runtime final {
public:
    bool Initialize(std::string& out_error);
    void Shutdown();
    void QueueSpawnProjectile(
        std::uint32_t owner_player_id,
        const command::FireProjectilePayload& payload);
    void QueueSpawnWorldDrop(const command::SpawnDropPayload& payload);
    void QueuePickupProbe(std::uint32_t player_id, const command::PickupProbePayload& payload);
    void Tick(const TickContext& tick_context);
    std::vector<CombatEvent> ConsumeCombatEvents();
    std::vector<GameplayEvent> ConsumeGameplayEvents();
    RuntimeDiagnostics DiagnosticsSnapshot() const;

private:
    struct ProjectileSpawnRequest final {
        std::uint32_t owner_player_id = 0;
        command::FireProjectilePayload payload{};
    };

    struct DamageRequest final {
        entt::entity target = entt::null;
        std::uint16_t damage = 0;
    };

    struct DropSpawnRequest final {
        command::SpawnDropPayload payload{};
    };

    struct PickupProbeRequest final {
        std::uint32_t player_id = 0;
        command::PickupProbePayload payload{};
    };

    void SpawnTrainingHostileTarget();
    void RunSpawnSystem();
    void RunDropSpawnSystem();
    void RunMovementSystem(double fixed_delta_seconds);
    void RunCollisionSystem();
    void RunDamageSystem();
    void RunPickupProbeSystem();
    void RunLifetimeRecycleSystem();
    void DestroyEntities(std::vector<entt::entity>& entities_to_destroy);

    bool initialized_ = false;
    entt::registry registry_{};
    std::vector<ProjectileSpawnRequest> pending_projectile_spawns_{};
    std::vector<DamageRequest> pending_damage_requests_{};
    std::vector<CombatEvent> pending_combat_events_{};
    std::vector<DropSpawnRequest> pending_drop_spawns_{};
    std::vector<PickupProbeRequest> pending_pickup_probes_{};
    std::vector<GameplayEvent> pending_gameplay_events_{};
    RuntimeDiagnostics diagnostics_{};
};

}  // namespace novaria::sim::ecs
