#pragma once

#include "sim/command_schema.h"
#include "sim/gameplay_types.h"
#include "sim/player_motion.h"
#include "core/tick_context.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace novaria::world {
class IWorldService;
}  // namespace novaria::world

namespace novaria::sim::ecs {

struct ActionPrimaryPlan final {
    bool is_harvest = false;
    bool is_place = false;
    std::uint16_t place_material_id = 0;
    int required_ticks = 0;
};

struct CraftRecipePlan final {
    int dirt_delta = 0;
    int stone_delta = 0;
    int wood_delta = 0;
    int coal_delta = 0;
    int torch_delta = 0;
    int workbench_delta = 0;
    int wood_sword_delta = 0;
    std::uint16_t crafted_material_id = 0;
    bool mark_workbench_built = false;
    bool mark_sword_crafted = false;
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
    std::uint16_t resource_id = 0;
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
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) noexcept;
    Runtime& operator=(Runtime&&) noexcept;

    bool Initialize(std::string& out_error);
    void Shutdown();

    void EnsurePlayer(std::uint32_t player_id);
    PlayerInventorySnapshot InventorySnapshot(std::uint32_t player_id) const;
    PlayerMotionSnapshot MotionSnapshot(std::uint32_t player_id) const;
    void SetPlayerMotionInput(std::uint32_t player_id, const PlayerMotionInput& input);
    void AddResourceToInventory(
        std::uint32_t player_id,
        std::uint16_t resource_id,
        std::uint32_t amount);
    bool TryCraftRecipePlan(
        std::uint32_t player_id,
        const CraftRecipePlan& plan,
        std::uint16_t& out_crafted_material_id);
    void ApplyActionPrimary(
        std::uint32_t player_id,
        const command::ActionPrimaryPayload& payload,
        const ActionPrimaryPlan& plan,
        std::uint16_t target_material_id,
        world::IWorldService& world_service);

    void QueueSpawnProjectile(
        std::uint32_t owner_player_id,
        const command::FireProjectilePayload& payload);
    void QueueSpawnWorldDrop(const command::SpawnDropPayload& payload);
    void QueuePickupProbe(std::uint32_t player_id, const command::PickupProbePayload& payload);
    void Tick(const core::TickContext& tick_context, const world::IWorldService& world_service);
    std::vector<CombatEvent> ConsumeCombatEvents();
    std::vector<GameplayEvent> ConsumeGameplayEvents();
    RuntimeDiagnostics DiagnosticsSnapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace novaria::sim::ecs
