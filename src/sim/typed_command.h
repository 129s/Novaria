#pragma once

#include "net/net_service.h"
#include "sim/command_schema.h"

#include <cstdint>

namespace novaria::sim {

enum class TypedPlayerCommandType : std::uint8_t {
    Unknown = 0,
    Jump,
    Attack,
    WorldSetTile,
    WorldLoadChunk,
    WorldUnloadChunk,
    GameplayCollectResource,
    GameplayBuildWorkbench,
    GameplayCraftSword,
    GameplayAttackEnemy,
    GameplayAttackBoss,
    CombatFireProjectile,
};

struct TypedPlayerCommand final {
    TypedPlayerCommandType type = TypedPlayerCommandType::Unknown;
    command::WorldSetTilePayload world_set_tile{};
    command::WorldChunkPayload world_chunk{};
    command::CollectResourcePayload collect_resource{};
    command::FireProjectilePayload fire_projectile{};
};

bool TryDecodePlayerCommand(
    const net::PlayerCommand& source_command,
    TypedPlayerCommand& out_typed_command);

}  // namespace novaria::sim
