#include "sim/ecs_runtime.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace novaria::sim::ecs {

bool Runtime::Initialize(std::string& out_error) {
    registry_.clear();
    pending_projectile_spawns_.clear();
    pending_damage_requests_.clear();
    pending_combat_events_.clear();
    pending_drop_spawns_.clear();
    pending_pickup_probes_.clear();
    pending_gameplay_events_.clear();
    diagnostics_ = {};

    SpawnTrainingHostileTarget();

    initialized_ = true;
    out_error.clear();
    return true;
}

void Runtime::Shutdown() {
    if (!initialized_) {
        return;
    }

    registry_.clear();
    pending_projectile_spawns_.clear();
    pending_damage_requests_.clear();
    pending_combat_events_.clear();
    pending_drop_spawns_.clear();
    pending_pickup_probes_.clear();
    pending_gameplay_events_.clear();
    diagnostics_ = {};
    initialized_ = false;
}

void Runtime::QueueSpawnProjectile(
    std::uint32_t owner_player_id,
    const command::FireProjectilePayload& payload) {
    if (!initialized_) {
        return;
    }

    pending_projectile_spawns_.push_back(ProjectileSpawnRequest{
        .owner_player_id = owner_player_id,
        .payload = payload,
    });
}

void Runtime::QueueSpawnWorldDrop(const command::SpawnDropPayload& payload) {
    if (!initialized_ || payload.amount == 0) {
        return;
    }

    pending_drop_spawns_.push_back(DropSpawnRequest{
        .payload = payload,
    });
}

void Runtime::QueuePickupProbe(
    std::uint32_t player_id,
    const command::PickupProbePayload& payload) {
    if (!initialized_) {
        return;
    }

    pending_pickup_probes_.push_back(PickupProbeRequest{
        .player_id = player_id,
        .payload = payload,
    });
}

void Runtime::Tick(const TickContext& tick_context) {
    if (!initialized_) {
        return;
    }

    RunSpawnSystem();
    RunDropSpawnSystem();
    RunMovementSystem(tick_context.fixed_delta_seconds);
    RunCollisionSystem();
    RunDamageSystem();
    RunPickupProbeSystem();
    RunLifetimeRecycleSystem();
}

std::vector<CombatEvent> Runtime::ConsumeCombatEvents() {
    std::vector<CombatEvent> events = std::move(pending_combat_events_);
    pending_combat_events_.clear();
    return events;
}

std::vector<GameplayEvent> Runtime::ConsumeGameplayEvents() {
    std::vector<GameplayEvent> events = std::move(pending_gameplay_events_);
    pending_gameplay_events_.clear();
    return events;
}

RuntimeDiagnostics Runtime::DiagnosticsSnapshot() const {
    RuntimeDiagnostics snapshot = diagnostics_;

    snapshot.active_projectile_count = 0;
    for (const entt::entity entity : registry_.view<const Projectile>()) {
        (void)entity;
        ++snapshot.active_projectile_count;
    }

    snapshot.active_hostile_count = 0;
    for (const entt::entity entity : registry_.view<const HostileTarget, const Health>()) {
        (void)entity;
        ++snapshot.active_hostile_count;
    }

    snapshot.active_drop_count = 0;
    for (const entt::entity entity : registry_.view<const WorldDrop, const Transform>()) {
        (void)entity;
        ++snapshot.active_drop_count;
    }

    return snapshot;
}

void Runtime::SpawnTrainingHostileTarget() {
    const entt::entity hostile = registry_.create();
    registry_.emplace<Transform>(hostile, Transform{
        .tile_x = 8.0F,
        .tile_y = -4.0F,
    });
    registry_.emplace<Collider>(hostile, Collider{
        .radius = 0.45F,
    });
    registry_.emplace<Health>(hostile, Health{
        .value = 25,
    });
    registry_.emplace<Faction>(hostile, Faction{
        .id = 2,
    });
    registry_.emplace<HostileTarget>(hostile, HostileTarget{
        .reward_kill_count = 1,
    });
}

void Runtime::RunSpawnSystem() {
    for (const ProjectileSpawnRequest& request : pending_projectile_spawns_) {
        const entt::entity projectile = registry_.create();
        registry_.emplace<Transform>(projectile, Transform{
            .tile_x = static_cast<float>(request.payload.origin_tile_x),
            .tile_y = static_cast<float>(request.payload.origin_tile_y),
        });
        registry_.emplace<Velocity>(projectile, Velocity{
            .tile_per_second_x =
                static_cast<float>(request.payload.velocity_milli_x) / 1000.0F,
            .tile_per_second_y =
                static_cast<float>(request.payload.velocity_milli_y) / 1000.0F,
        });
        registry_.emplace<Collider>(projectile, Collider{
            .radius = 0.2F,
        });
        registry_.emplace<Faction>(projectile, Faction{
            .id = request.payload.faction,
        });
        registry_.emplace<Lifetime>(projectile, Lifetime{
            .ticks_remaining = request.payload.lifetime_ticks,
        });
        registry_.emplace<Projectile>(projectile, Projectile{
            .owner_player_id = request.owner_player_id,
            .damage = request.payload.damage,
        });
        ++diagnostics_.total_projectile_spawned;
    }

    pending_projectile_spawns_.clear();
}

void Runtime::RunDropSpawnSystem() {
    for (const DropSpawnRequest& request : pending_drop_spawns_) {
        if (request.payload.material_id == 0 || request.payload.amount == 0) {
            continue;
        }

        bool merged = false;
        auto drop_view = registry_.view<Transform, WorldDrop>();
        for (const entt::entity entity : drop_view) {
            auto& transform = drop_view.get<Transform>(entity);
            auto& world_drop = drop_view.get<WorldDrop>(entity);
            if (static_cast<int>(transform.tile_x) != request.payload.tile_x ||
                static_cast<int>(transform.tile_y) != request.payload.tile_y ||
                world_drop.material_id != request.payload.material_id) {
                continue;
            }

            world_drop.amount += request.payload.amount;
            merged = true;
            break;
        }

        if (!merged) {
            const entt::entity drop_entity = registry_.create();
            registry_.emplace<Transform>(drop_entity, Transform{
                .tile_x = static_cast<float>(request.payload.tile_x),
                .tile_y = static_cast<float>(request.payload.tile_y),
            });
            registry_.emplace<WorldDrop>(drop_entity, WorldDrop{
                .material_id = request.payload.material_id,
                .amount = request.payload.amount,
            });
        }

        diagnostics_.total_drop_spawned += request.payload.amount;
    }

    pending_drop_spawns_.clear();
}

void Runtime::RunMovementSystem(double fixed_delta_seconds) {
    auto movement_view = registry_.view<Transform, const Velocity>();
    for (const entt::entity entity : movement_view) {
        auto& transform = movement_view.get<Transform>(entity);
        const auto& velocity = movement_view.get<const Velocity>(entity);
        transform.tile_x += velocity.tile_per_second_x * static_cast<float>(fixed_delta_seconds);
        transform.tile_y += velocity.tile_per_second_y * static_cast<float>(fixed_delta_seconds);
    }
}

void Runtime::RunCollisionSystem() {
    auto projectile_view = registry_.view<const Transform, const Collider, const Projectile, const Faction>();
    auto hostile_view = registry_.view<const Transform, const Collider, const Health, const Faction, const HostileTarget>();

    std::vector<entt::entity> consumed_projectiles;
    for (const entt::entity projectile_entity : projectile_view) {
        const auto& projectile_transform =
            projectile_view.get<const Transform>(projectile_entity);
        const auto& projectile_collider =
            projectile_view.get<const Collider>(projectile_entity);
        const auto& projectile_data =
            projectile_view.get<const Projectile>(projectile_entity);
        const auto& projectile_faction =
            projectile_view.get<const Faction>(projectile_entity);

        for (const entt::entity hostile_entity : hostile_view) {
            const auto& hostile_faction = hostile_view.get<const Faction>(hostile_entity);
            if (hostile_faction.id == projectile_faction.id) {
                continue;
            }

            const auto& hostile_transform = hostile_view.get<const Transform>(hostile_entity);
            const auto& hostile_collider = hostile_view.get<const Collider>(hostile_entity);
            const float delta_x = projectile_transform.tile_x - hostile_transform.tile_x;
            const float delta_y = projectile_transform.tile_y - hostile_transform.tile_y;
            const float collision_radius = projectile_collider.radius + hostile_collider.radius;
            const float distance_squared = delta_x * delta_x + delta_y * delta_y;
            if (distance_squared > collision_radius * collision_radius) {
                continue;
            }

            pending_damage_requests_.push_back(DamageRequest{
                .target = hostile_entity,
                .damage = projectile_data.damage,
            });
            consumed_projectiles.push_back(projectile_entity);
            break;
        }
    }

    const std::size_t recycled_projectile_count = consumed_projectiles.size();
    DestroyEntities(consumed_projectiles);
    diagnostics_.total_projectile_recycled += recycled_projectile_count;
}

void Runtime::RunDamageSystem() {
    for (const DamageRequest& request : pending_damage_requests_) {
        if (request.target == entt::null || !registry_.valid(request.target)) {
            continue;
        }
        if (!registry_.all_of<Health>(request.target)) {
            continue;
        }

        ++diagnostics_.total_damage_instances;
        auto& health = registry_.get<Health>(request.target);
        health.value -= static_cast<std::int32_t>(request.damage);
        if (health.value > 0) {
            continue;
        }

        std::uint16_t reward_kill_count = 0;
        if (registry_.all_of<HostileTarget>(request.target)) {
            reward_kill_count = registry_.get<HostileTarget>(request.target).reward_kill_count;
            diagnostics_.total_hostile_defeated += reward_kill_count;
        }

        pending_combat_events_.push_back(CombatEvent{
            .type = CombatEventType::HostileDefeated,
            .reward_kill_count = reward_kill_count,
        });
        registry_.destroy(request.target);
    }

    pending_damage_requests_.clear();
}

void Runtime::RunPickupProbeSystem() {
    if (pending_pickup_probes_.empty()) {
        return;
    }

    auto drop_view = registry_.view<const Transform, const WorldDrop>();
    if (drop_view.begin() == drop_view.end()) {
        pending_pickup_probes_.clear();
        return;
    }

    std::vector<entt::entity> consumed_drop_entities;
    for (const PickupProbeRequest& request : pending_pickup_probes_) {
        for (const entt::entity entity : drop_view) {
            if (std::find(
                    consumed_drop_entities.begin(),
                    consumed_drop_entities.end(),
                    entity) != consumed_drop_entities.end()) {
                continue;
            }

            const auto& transform = drop_view.get<const Transform>(entity);
            if (static_cast<int>(transform.tile_x) != request.payload.tile_x ||
                static_cast<int>(transform.tile_y) != request.payload.tile_y) {
                continue;
            }

            const auto& drop_data = drop_view.get<const WorldDrop>(entity);
            pending_gameplay_events_.push_back(GameplayEvent{
                .type = GameplayEventType::PickupResolved,
                .player_id = request.player_id,
                .tile_x = request.payload.tile_x,
                .tile_y = request.payload.tile_y,
                .material_id = drop_data.material_id,
                .amount = drop_data.amount,
            });
            diagnostics_.total_drop_picked_up += drop_data.amount;
            consumed_drop_entities.push_back(entity);
        }
    }

    pending_pickup_probes_.clear();
    DestroyEntities(consumed_drop_entities);
}

void Runtime::RunLifetimeRecycleSystem() {
    auto lifetime_view = registry_.view<Lifetime>();
    std::vector<entt::entity> expired_entities;
    for (const entt::entity entity : lifetime_view) {
        auto& lifetime = lifetime_view.get<Lifetime>(entity);
        if (lifetime.ticks_remaining == 0) {
            expired_entities.push_back(entity);
            continue;
        }

        --lifetime.ticks_remaining;
        if (lifetime.ticks_remaining == 0) {
            expired_entities.push_back(entity);
        }
    }

    const std::size_t recycled_projectile_count = std::count_if(
        expired_entities.begin(),
        expired_entities.end(),
        [this](entt::entity entity) { return registry_.all_of<Projectile>(entity); });
    DestroyEntities(expired_entities);
    diagnostics_.total_projectile_recycled += recycled_projectile_count;
}

void Runtime::DestroyEntities(std::vector<entt::entity>& entities_to_destroy) {
    if (entities_to_destroy.empty()) {
        return;
    }

    std::sort(entities_to_destroy.begin(), entities_to_destroy.end());
    entities_to_destroy.erase(
        std::unique(entities_to_destroy.begin(), entities_to_destroy.end()),
        entities_to_destroy.end());

    for (const entt::entity entity : entities_to_destroy) {
        if (!registry_.valid(entity)) {
            continue;
        }
        registry_.destroy(entity);
    }
}

}  // namespace novaria::sim::ecs
