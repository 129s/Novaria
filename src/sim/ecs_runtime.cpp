#include "sim/ecs_runtime.h"

#include "sim/tile_collision.h"
#include "world/material_catalog.h"
#include "world/world_service.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace novaria::sim::ecs {
namespace {

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

struct PlayerId final {
    std::uint32_t value = 0;
};

struct PlayerInventory final {
    std::uint32_t dirt_count = 0;
    std::uint32_t stone_count = 0;
    std::uint32_t wood_count = 0;
    std::uint32_t coal_count = 0;
    std::uint32_t torch_count = 0;
    std::uint32_t workbench_count = 0;
    std::uint32_t wood_sword_count = 0;
    bool has_pickaxe_tool = true;
    bool has_axe_tool = true;
};

struct PrimaryActionProgress final {
    bool active = false;
    bool is_harvest = false;
    bool is_place = false;
    int target_tile_x = 0;
    int target_tile_y = 0;
    std::uint16_t target_material_id = 0;
    std::uint16_t place_material_id = 0;
    std::uint8_t hotbar_row = 0;
    std::uint8_t hotbar_slot = 0;
    int required_ticks = 0;
    int elapsed_ticks = 0;
};

struct PlayerMotion final {
    PlayerMotionState state{};
    PlayerMotionInput input{};
    bool pending_jump_pressed = false;
};

}  // namespace

struct Runtime::Impl final {
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

    bool initialized = false;
    entt::registry registry{};
    std::unordered_map<std::uint32_t, entt::entity> player_entities{};
    std::vector<ProjectileSpawnRequest> pending_projectile_spawns{};
    std::vector<DamageRequest> pending_damage_requests{};
    std::vector<CombatEvent> pending_combat_events{};
    std::vector<DropSpawnRequest> pending_drop_spawns{};
    std::vector<PickupProbeRequest> pending_pickup_probes{};
    std::vector<GameplayEvent> pending_gameplay_events{};
    RuntimeDiagnostics diagnostics{};

    void ResetState() {
        registry.clear();
        player_entities.clear();
        pending_projectile_spawns.clear();
        pending_damage_requests.clear();
        pending_combat_events.clear();
        pending_drop_spawns.clear();
        pending_pickup_probes.clear();
        pending_gameplay_events.clear();
        diagnostics = {};
    }

    entt::entity EnsurePlayerEntity(std::uint32_t player_id) {
        if (player_id == 0) {
            return entt::null;
        }

        const auto iter = player_entities.find(player_id);
        if (iter != player_entities.end() && registry.valid(iter->second)) {
            return iter->second;
        }

        const entt::entity entity = registry.create();
        registry.emplace<PlayerId>(entity, PlayerId{.value = player_id});
        registry.emplace<Transform>(entity, Transform{.tile_x = 0.0F, .tile_y = -2.0F});
        registry.emplace<PlayerInventory>(entity, PlayerInventory{});
        registry.emplace<PrimaryActionProgress>(entity, PrimaryActionProgress{});
        registry.emplace<PlayerMotion>(entity, PlayerMotion{});
        player_entities[player_id] = entity;
        return entity;
    }

    entt::entity FindPlayerEntity(std::uint32_t player_id) const {
        const auto iter = player_entities.find(player_id);
        if (iter == player_entities.end()) {
            return entt::null;
        }
        if (!registry.valid(iter->second)) {
            return entt::null;
        }
        return iter->second;
    }

    void AddPickupToInventory(
        std::uint32_t player_id,
        std::uint16_t material_id,
        std::uint32_t amount) {
        const entt::entity entity = EnsurePlayerEntity(player_id);
        if (entity == entt::null || !registry.all_of<PlayerInventory>(entity)) {
            return;
        }

        auto& inventory = registry.get<PlayerInventory>(entity);
        if (material_id == world::material::kDirt || material_id == world::material::kGrass) {
            inventory.dirt_count += amount;
            return;
        }
        if (material_id == world::material::kStone) {
            inventory.stone_count += amount;
            return;
        }
        if (material_id == world::material::kWood) {
            inventory.wood_count += amount;
            return;
        }
        if (material_id == world::material::kCoalOre) {
            inventory.coal_count += amount;
            return;
        }
        if (material_id == world::material::kTorch) {
            inventory.torch_count += amount;
            return;
        }
        if (material_id == world::material::kWorkbench) {
            inventory.workbench_count += amount;
            return;
        }
    }

    void DestroyEntities(std::vector<entt::entity>& entities_to_destroy) {
        for (const entt::entity entity : entities_to_destroy) {
            if (!registry.valid(entity)) {
                continue;
            }
            if (registry.all_of<Projectile>(entity)) {
                ++diagnostics.total_projectile_recycled;
            }
            registry.destroy(entity);
        }
    }

    void SpawnTrainingHostileTarget() {
        const entt::entity hostile = registry.create();
        registry.emplace<Transform>(hostile, Transform{.tile_x = 6.5F, .tile_y = -4.0F});
        registry.emplace<Collider>(hostile, Collider{.radius = 0.45F});
        registry.emplace<Health>(hostile, Health{.value = 20});
        registry.emplace<HostileTarget>(hostile, HostileTarget{.reward_kill_count = 2});
        registry.emplace<Faction>(hostile, Faction{.id = 2});
    }

    void RunSpawnSystem() {
        for (const ProjectileSpawnRequest& spawn : pending_projectile_spawns) {
            const entt::entity projectile = registry.create();
            registry.emplace<Transform>(
                projectile,
                Transform{
                    .tile_x = static_cast<float>(spawn.payload.origin_tile_x) + 0.5F,
                    .tile_y = static_cast<float>(spawn.payload.origin_tile_y) + 0.5F,
                });
            registry.emplace<Velocity>(
                projectile,
                Velocity{
                    .tile_per_second_x = static_cast<float>(spawn.payload.velocity_milli_x) / 1000.0F,
                    .tile_per_second_y = static_cast<float>(spawn.payload.velocity_milli_y) / 1000.0F,
                });
            registry.emplace<Collider>(projectile, Collider{.radius = 0.35F});
            registry.emplace<Lifetime>(
                projectile,
                Lifetime{.ticks_remaining = spawn.payload.lifetime_ticks});
            registry.emplace<Projectile>(
                projectile,
                Projectile{
                    .owner_player_id = spawn.owner_player_id,
                    .damage = spawn.payload.damage,
                });
            registry.emplace<Faction>(projectile, Faction{.id = spawn.payload.faction});
            ++diagnostics.total_projectile_spawned;
        }

        pending_projectile_spawns.clear();
    }

    void RunDropSpawnSystem() {
        for (const DropSpawnRequest& spawn : pending_drop_spawns) {
            const entt::entity drop_entity = registry.create();
            registry.emplace<Transform>(
                drop_entity,
                Transform{
                    .tile_x = static_cast<float>(spawn.payload.tile_x) + 0.5F,
                    .tile_y = static_cast<float>(spawn.payload.tile_y) + 0.5F,
                });
            registry.emplace<Collider>(drop_entity, Collider{.radius = 0.45F});
            registry.emplace<WorldDrop>(
                drop_entity,
                WorldDrop{
                    .material_id = spawn.payload.material_id,
                    .amount = spawn.payload.amount,
                });
            diagnostics.total_drop_spawned += spawn.payload.amount;
        }

        pending_drop_spawns.clear();
    }

    void RunPlayerMotionSystem(
        const world::IWorldService& world_service,
        double fixed_delta_seconds) {
        for (const entt::entity entity : registry.view<PlayerMotion, Transform>()) {
            auto& motion = registry.get<PlayerMotion>(entity);
            auto& transform = registry.get<Transform>(entity);

            PlayerMotionInput input = motion.input;
            if (motion.pending_jump_pressed) {
                input.jump_pressed = true;
                motion.pending_jump_pressed = false;
            }

            const PlayerMotionSettings settings = DefaultPlayerMotionSettings();
            UpdatePlayerMotion(
                input,
                settings,
                world_service,
                fixed_delta_seconds,
                motion.state);

            const PlayerMotionSnapshot next_snapshot = SnapshotPlayerMotion(motion.state);
            transform.tile_x = next_snapshot.position_x;
            transform.tile_y = next_snapshot.position_y;
        }
    }

    void RunMovementSystem(double fixed_delta_seconds) {
        for (const entt::entity entity : registry.view<Transform, const Velocity>()) {
            auto& transform = registry.get<Transform>(entity);
            const auto& velocity = registry.get<const Velocity>(entity);
            transform.tile_x += velocity.tile_per_second_x * static_cast<float>(fixed_delta_seconds);
            transform.tile_y += velocity.tile_per_second_y * static_cast<float>(fixed_delta_seconds);
        }
    }

    void RunCollisionSystem() {
        const auto projectile_view = registry.view<const Projectile, const Transform, const Collider, const Faction>();
        const auto hostile_view = registry.view<const HostileTarget, const Transform, const Collider, const Faction, Health>();
        const auto drop_view = registry.view<const WorldDrop, const Transform, const Collider>();

        for (const entt::entity projectile_entity : projectile_view) {
            const auto& projectile = projectile_view.get<const Projectile>(projectile_entity);
            const auto& projectile_transform = projectile_view.get<const Transform>(projectile_entity);
            const auto& projectile_collider = projectile_view.get<const Collider>(projectile_entity);
            const auto& projectile_faction = projectile_view.get<const Faction>(projectile_entity);

            for (const entt::entity hostile_entity : hostile_view) {
                const auto& hostile_transform = hostile_view.get<const Transform>(hostile_entity);
                const auto& hostile_collider = hostile_view.get<const Collider>(hostile_entity);
                const auto& hostile_faction = hostile_view.get<const Faction>(hostile_entity);

                if (projectile_faction.id == hostile_faction.id) {
                    continue;
                }

                const float delta_x = projectile_transform.tile_x - hostile_transform.tile_x;
                const float delta_y = projectile_transform.tile_y - hostile_transform.tile_y;
                const float distance_sq = delta_x * delta_x + delta_y * delta_y;
                const float radius_sum = projectile_collider.radius + hostile_collider.radius;
                if (distance_sq > radius_sum * radius_sum) {
                    continue;
                }

                pending_damage_requests.push_back(DamageRequest{
                    .target = hostile_entity,
                    .damage = projectile.damage,
                });

                registry.emplace_or_replace<Lifetime>(projectile_entity, Lifetime{.ticks_remaining = 0});
                ++diagnostics.total_damage_instances;
                break;
            }
        }

        (void)drop_view;
    }

    void RunDamageSystem() {
        for (const DamageRequest& request : pending_damage_requests) {
            if (request.target == entt::null || !registry.valid(request.target)) {
                continue;
            }

            auto* health = registry.try_get<Health>(request.target);
            if (health == nullptr) {
                continue;
            }

            health->value -= static_cast<std::int32_t>(request.damage);
            if (health->value > 0) {
                continue;
            }

            if (registry.all_of<HostileTarget>(request.target)) {
                const auto& target = registry.get<const HostileTarget>(request.target);
                pending_combat_events.push_back(CombatEvent{
                    .type = CombatEventType::HostileDefeated,
                    .reward_kill_count = target.reward_kill_count,
                });
                ++diagnostics.total_hostile_defeated;
            }

            registry.destroy(request.target);
        }

        pending_damage_requests.clear();
    }

    void RunPickupProbeSystem() {
        const auto drop_view = registry.view<const WorldDrop, const Transform, const Collider>();
        std::vector<entt::entity> consumed_drop_entities;
        for (const PickupProbeRequest& probe : pending_pickup_probes) {
            float probe_tile_x = static_cast<float>(probe.payload.tile_x) + 0.5F;
            float probe_tile_y = static_cast<float>(probe.payload.tile_y) + 0.5F;
            const entt::entity player = FindPlayerEntity(probe.player_id);
            if (player != entt::null && registry.all_of<Transform>(player)) {
                const auto& player_transform = registry.get<const Transform>(player);
                probe_tile_x = player_transform.tile_x;
                probe_tile_y = player_transform.tile_y;
            }

            for (const entt::entity entity : drop_view) {
                const auto& drop = drop_view.get<const WorldDrop>(entity);
                const auto& transform = drop_view.get<const Transform>(entity);
                const auto& collider = drop_view.get<const Collider>(entity);

                const float drop_tile_x = transform.tile_x;
                const float drop_tile_y = transform.tile_y;
                const float delta_x = drop_tile_x - probe_tile_x;
                const float delta_y = drop_tile_y - probe_tile_y;
                const float distance_sq = delta_x * delta_x + delta_y * delta_y;
                constexpr float kPlayerPickupRadius = 0.45F;
                const float radius_sum = collider.radius + kPlayerPickupRadius;
                if (distance_sq > radius_sum * radius_sum) {
                    continue;
                }

                AddPickupToInventory(probe.player_id, drop.material_id, drop.amount);
                diagnostics.total_drop_picked_up += drop.amount;
                pending_gameplay_events.push_back(GameplayEvent{
                    .type = GameplayEventType::PickupResolved,
                    .player_id = probe.player_id,
                    .tile_x = probe.payload.tile_x,
                    .tile_y = probe.payload.tile_y,
                    .material_id = drop.material_id,
                    .resource_id = 0,
                    .amount = drop.amount,
                });
                consumed_drop_entities.push_back(entity);
                break;
            }
        }

        DestroyEntities(consumed_drop_entities);
        pending_pickup_probes.clear();
    }

    void RunLifetimeRecycleSystem() {
        std::vector<entt::entity> expired_entities;
        for (const entt::entity entity : registry.view<Lifetime>()) {
            auto& lifetime = registry.get<Lifetime>(entity);
            if (lifetime.ticks_remaining > 0) {
                --lifetime.ticks_remaining;
                continue;
            }
            expired_entities.push_back(entity);
        }

        DestroyEntities(expired_entities);
    }
};

Runtime::Runtime() : impl_(std::make_unique<Impl>()) {}

Runtime::~Runtime() = default;

Runtime::Runtime(Runtime&&) noexcept = default;

Runtime& Runtime::operator=(Runtime&&) noexcept = default;

bool Runtime::Initialize(std::string& out_error) {
    impl_->ResetState();
    impl_->SpawnTrainingHostileTarget();

    impl_->initialized = true;
    out_error.clear();
    return true;
}

void Runtime::Shutdown() {
    if (!impl_->initialized) {
        return;
    }

    impl_->ResetState();
    impl_->initialized = false;
}

void Runtime::EnsurePlayer(std::uint32_t player_id) {
    if (!impl_->initialized || player_id == 0) {
        return;
    }

    (void)impl_->EnsurePlayerEntity(player_id);
}

PlayerInventorySnapshot Runtime::InventorySnapshot(std::uint32_t player_id) const {
    const entt::entity entity = impl_->FindPlayerEntity(player_id);
    if (entity == entt::null || !impl_->registry.all_of<PlayerInventory>(entity)) {
        return PlayerInventorySnapshot{};
    }

    const auto& inventory = impl_->registry.get<const PlayerInventory>(entity);
    return PlayerInventorySnapshot{
        .dirt_count = inventory.dirt_count,
        .stone_count = inventory.stone_count,
        .wood_count = inventory.wood_count,
        .coal_count = inventory.coal_count,
        .torch_count = inventory.torch_count,
        .workbench_count = inventory.workbench_count,
        .wood_sword_count = inventory.wood_sword_count,
        .has_pickaxe_tool = inventory.has_pickaxe_tool,
        .has_axe_tool = inventory.has_axe_tool,
    };
}

ActionPrimaryProgressSnapshot Runtime::ActionPrimaryProgressSnapshot(std::uint32_t player_id) const {
    const entt::entity entity = impl_->FindPlayerEntity(player_id);
    if (entity == entt::null || !impl_->registry.all_of<PrimaryActionProgress>(entity)) {
        return ::novaria::sim::ActionPrimaryProgressSnapshot{};
    }

    const auto& progress = impl_->registry.get<const PrimaryActionProgress>(entity);
    return ::novaria::sim::ActionPrimaryProgressSnapshot{
        .active = progress.active,
        .is_harvest = progress.is_harvest,
        .is_place = progress.is_place,
        .target_tile_x = progress.target_tile_x,
        .target_tile_y = progress.target_tile_y,
        .target_material_id = progress.target_material_id,
        .place_material_id = progress.place_material_id,
        .hotbar_row = progress.hotbar_row,
        .hotbar_slot = progress.hotbar_slot,
        .required_ticks = progress.required_ticks,
        .elapsed_ticks = progress.elapsed_ticks,
    };
}

PlayerMotionSnapshot Runtime::MotionSnapshot(std::uint32_t player_id) const {
    const entt::entity entity = impl_->FindPlayerEntity(player_id);
    if (entity == entt::null || !impl_->registry.all_of<PlayerMotion>(entity)) {
        return PlayerMotionSnapshot{};
    }

    const auto& motion = impl_->registry.get<const PlayerMotion>(entity);
    return SnapshotPlayerMotion(motion.state);
}

void Runtime::SetPlayerMotionInput(std::uint32_t player_id, const PlayerMotionInput& input) {
    if (!impl_->initialized || player_id == 0) {
        return;
    }

    const entt::entity player = impl_->EnsurePlayerEntity(player_id);
    if (player == entt::null || !impl_->registry.all_of<PlayerMotion>(player)) {
        return;
    }

    auto& motion = impl_->registry.get<PlayerMotion>(player);
    motion.input = input;
    if (input.jump_pressed) {
        motion.pending_jump_pressed = true;
    }
}

void Runtime::AddResourceToInventory(
    std::uint32_t player_id,
    std::uint16_t resource_id,
    std::uint32_t amount) {
    if (!impl_->initialized || player_id == 0) {
        return;
    }

    const entt::entity entity = impl_->EnsurePlayerEntity(player_id);
    if (entity == entt::null || !impl_->registry.all_of<PlayerInventory>(entity)) {
        return;
    }

    auto& inventory = impl_->registry.get<PlayerInventory>(entity);
    if (resource_id == command::kResourceStone) {
        inventory.stone_count += amount;
    } else if (resource_id == command::kResourceWood) {
        inventory.wood_count += amount;
    }
}

bool Runtime::TryCraftRecipePlan(
    std::uint32_t player_id,
    const CraftRecipePlan& plan,
    std::uint16_t& out_crafted_material_id) {
    out_crafted_material_id = 0;
    if (!impl_->initialized || player_id == 0) {
        return false;
    }

    const entt::entity entity = impl_->EnsurePlayerEntity(player_id);
    if (entity == entt::null || !impl_->registry.all_of<PlayerInventory>(entity)) {
        return false;
    }

    auto& inventory = impl_->registry.get<PlayerInventory>(entity);
    auto would_underflow = [](std::uint32_t current, int delta) {
        if (delta >= 0) {
            return false;
        }
        const std::uint32_t required = static_cast<std::uint32_t>(-delta);
        return current < required;
    };

    if (would_underflow(inventory.dirt_count, plan.dirt_delta) ||
        would_underflow(inventory.stone_count, plan.stone_delta) ||
        would_underflow(inventory.wood_count, plan.wood_delta) ||
        would_underflow(inventory.coal_count, plan.coal_delta) ||
        would_underflow(inventory.torch_count, plan.torch_delta) ||
        would_underflow(inventory.workbench_count, plan.workbench_delta) ||
        would_underflow(inventory.wood_sword_count, plan.wood_sword_delta)) {
        return false;
    }

    inventory.dirt_count = static_cast<std::uint32_t>(
        static_cast<int>(inventory.dirt_count) + plan.dirt_delta);
    inventory.stone_count = static_cast<std::uint32_t>(
        static_cast<int>(inventory.stone_count) + plan.stone_delta);
    inventory.wood_count = static_cast<std::uint32_t>(
        static_cast<int>(inventory.wood_count) + plan.wood_delta);
    inventory.coal_count = static_cast<std::uint32_t>(
        static_cast<int>(inventory.coal_count) + plan.coal_delta);
    inventory.torch_count = static_cast<std::uint32_t>(
        static_cast<int>(inventory.torch_count) + plan.torch_delta);
    inventory.workbench_count = static_cast<std::uint32_t>(
        static_cast<int>(inventory.workbench_count) + plan.workbench_delta);
    inventory.wood_sword_count = static_cast<std::uint32_t>(
        static_cast<int>(inventory.wood_sword_count) + plan.wood_sword_delta);

    out_crafted_material_id = plan.crafted_material_id;
    return true;
}

void Runtime::ApplyActionPrimary(
    std::uint32_t player_id,
    const command::ActionPrimaryPayload& payload,
    const ActionPrimaryPlan& plan,
    std::uint16_t target_material_id,
    world::IWorldService& world_service) {
    if (!impl_->initialized || player_id == 0) {
        return;
    }

    const entt::entity player = impl_->EnsurePlayerEntity(player_id);
    if (player == entt::null || !impl_->registry.all_of<PrimaryActionProgress>(player)) {
        return;
    }

    auto& progress = impl_->registry.get<PrimaryActionProgress>(player);
    if (!plan.is_harvest && !plan.is_place) {
        progress = {};
        return;
    }

    if (!progress.active ||
        progress.target_tile_x != payload.target_tile_x ||
        progress.target_tile_y != payload.target_tile_y ||
        progress.hotbar_row != payload.hotbar_row ||
        progress.hotbar_slot != payload.hotbar_slot ||
        progress.target_material_id != target_material_id) {
        progress = PrimaryActionProgress{
            .active = true,
            .is_harvest = plan.is_harvest,
            .is_place = plan.is_place,
            .target_tile_x = payload.target_tile_x,
            .target_tile_y = payload.target_tile_y,
            .target_material_id = target_material_id,
            .place_material_id = plan.place_material_id,
            .hotbar_row = payload.hotbar_row,
            .hotbar_slot = payload.hotbar_slot,
            .required_ticks = plan.required_ticks,
            .elapsed_ticks = 0,
        };
    }

    if (progress.required_ticks <= 0) {
        progress = {};
        return;
    }

    ++progress.elapsed_ticks;
    if (progress.elapsed_ticks < progress.required_ticks) {
        return;
    }

    if (progress.is_harvest) {
        std::string error;
        (void)world_service.ApplyTileMutation(
            world::TileMutation{
                .tile_x = progress.target_tile_x,
                .tile_y = progress.target_tile_y,
                .material_id = world::material::kAir,
            },
            error);

        const std::uint16_t drop_material =
            target_material_id == world::material::kGrass ? world::material::kDirt : target_material_id;
        impl_->pending_drop_spawns.push_back(Impl::DropSpawnRequest{
            .payload = command::SpawnDropPayload{
                .tile_x = progress.target_tile_x,
                .tile_y = progress.target_tile_y,
                .material_id = drop_material,
                .amount = 1,
            },
        });
    } else if (progress.is_place) {
        if (world::material::CollisionShapeFor(progress.place_material_id) !=
            world::material::CollisionShape::Empty) {
            const auto* motion = impl_->registry.try_get<PlayerMotion>(player);
            if (motion != nullptr) {
                const PlayerMotionSettings& settings = DefaultPlayerMotionSettings();
                const collision::Aabb player_aabb = collision::MakePlayerAabb(
                    motion->state.position_x,
                    motion->state.position_y,
                    settings.half_width,
                    settings.height,
                    0.002F);
                if (collision::WouldMaterialOverlapAabb(
                        static_cast<world::material::MaterialId>(progress.place_material_id),
                        progress.target_tile_x,
                        progress.target_tile_y,
                        player_aabb)) {
                    progress = {};
                    return;
                }
            }
        }

        std::string error;
        if (!world_service.ApplyTileMutation(
                world::TileMutation{
                    .tile_x = progress.target_tile_x,
                    .tile_y = progress.target_tile_y,
                    .material_id = progress.place_material_id,
                },
                error)) {
            progress = {};
            return;
        }

        const entt::entity entity = impl_->EnsurePlayerEntity(player_id);
        if (entity != entt::null && impl_->registry.all_of<PlayerInventory>(entity)) {
            auto& inventory = impl_->registry.get<PlayerInventory>(entity);
            if (progress.place_material_id == world::material::kDirt && inventory.dirt_count > 0) {
                --inventory.dirt_count;
            } else if (progress.place_material_id == world::material::kStone && inventory.stone_count > 0) {
                --inventory.stone_count;
            } else if (progress.place_material_id == world::material::kTorch && inventory.torch_count > 0) {
                --inventory.torch_count;
            } else if (progress.place_material_id == world::material::kWorkbench && inventory.workbench_count > 0) {
                --inventory.workbench_count;
            }
        }
    }

    progress = {};
}

void Runtime::QueueSpawnProjectile(
    std::uint32_t owner_player_id,
    const command::FireProjectilePayload& payload) {
    if (!impl_->initialized || owner_player_id == 0) {
        return;
    }

    impl_->pending_projectile_spawns.push_back(Impl::ProjectileSpawnRequest{
        .owner_player_id = owner_player_id,
        .payload = payload,
    });
}

void Runtime::QueueSpawnWorldDrop(const command::SpawnDropPayload& payload) {
    if (!impl_->initialized) {
        return;
    }

    impl_->pending_drop_spawns.push_back(Impl::DropSpawnRequest{.payload = payload});
}

void Runtime::QueuePickupProbe(std::uint32_t player_id, const command::PickupProbePayload& payload) {
    if (!impl_->initialized || player_id == 0) {
        return;
    }

    impl_->pending_pickup_probes.push_back(Impl::PickupProbeRequest{
        .player_id = player_id,
        .payload = payload,
    });
}

void Runtime::Tick(const core::TickContext& tick_context, const world::IWorldService& world_service) {
    if (!impl_->initialized) {
        return;
    }

    impl_->RunSpawnSystem();
    impl_->RunDropSpawnSystem();
    impl_->RunPlayerMotionSystem(world_service, tick_context.fixed_delta_seconds);
    impl_->RunMovementSystem(tick_context.fixed_delta_seconds);
    impl_->RunCollisionSystem();
    impl_->RunDamageSystem();
    impl_->RunPickupProbeSystem();
    impl_->RunLifetimeRecycleSystem();

    impl_->diagnostics.active_projectile_count = impl_->registry.view<const Projectile>().size();
    impl_->diagnostics.active_hostile_count = impl_->registry.view<const HostileTarget>().size();
    impl_->diagnostics.active_drop_count = impl_->registry.view<const WorldDrop>().size();
}

std::vector<CombatEvent> Runtime::ConsumeCombatEvents() {
    std::vector<CombatEvent> events = std::move(impl_->pending_combat_events);
    impl_->pending_combat_events.clear();
    return events;
}

std::vector<GameplayEvent> Runtime::ConsumeGameplayEvents() {
    std::vector<GameplayEvent> events = std::move(impl_->pending_gameplay_events);
    impl_->pending_gameplay_events.clear();
    return events;
}

RuntimeDiagnostics Runtime::DiagnosticsSnapshot() const {
    return impl_->diagnostics;
}

}  // namespace novaria::sim::ecs
