#pragma once

#include "net/net_service.h"
#include "sim/command_schema.h"

#include <cstdint>

namespace novaria::sim {

enum class TypedPlayerCommandType : std::uint8_t {
    Unknown = 0,
    Jump,
    Attack,
    PlayerMotionInput,
    WorldSetTile,
    WorldLoadChunk,
    WorldUnloadChunk,
    GameplayCollectResource,
    GameplaySpawnDrop,
    GameplayPickupProbe,
    GameplayInteraction,
    GameplayActionPrimary,
    GameplayCraftRecipe,
    GameplayAttackEnemy,
    GameplayAttackBoss,
    CombatFireProjectile,
};

struct TypedPlayerCommand final {
    TypedPlayerCommandType type = TypedPlayerCommandType::Unknown;
    command::PlayerMotionInputPayload player_motion_input{};
    command::WorldSetTilePayload world_set_tile{};
    command::WorldChunkPayload world_chunk{};
    command::CollectResourcePayload collect_resource{};
    command::SpawnDropPayload spawn_drop{};
    command::PickupProbePayload pickup_probe{};
    command::InteractionPayload interaction{};
    command::ActionPrimaryPayload action_primary{};
    command::CraftRecipePayload craft_recipe{};
    command::FireProjectilePayload fire_projectile{};
};

bool TryDecodePlayerCommand(
    const net::PlayerCommand& source_command,
    TypedPlayerCommand& out_typed_command);

}  // namespace novaria::sim
