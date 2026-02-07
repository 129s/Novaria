#include "sim/typed_command.h"

namespace novaria::sim {

bool TryDecodePlayerCommand(
    const net::PlayerCommand& source_command,
    TypedPlayerCommand& out_typed_command) {
    out_typed_command = {};

    if (source_command.command_type == command::kJump) {
        out_typed_command.type = TypedPlayerCommandType::Jump;
        return true;
    }

    if (source_command.command_type == command::kAttack) {
        out_typed_command.type = TypedPlayerCommandType::Attack;
        return true;
    }

    if (source_command.command_type == command::kWorldSetTile) {
        if (!command::TryParseWorldSetTilePayload(
                source_command.payload,
                out_typed_command.world_set_tile)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::WorldSetTile;
        return true;
    }

    if (source_command.command_type == command::kWorldLoadChunk) {
        if (!command::TryParseWorldChunkPayload(
                source_command.payload,
                out_typed_command.world_chunk)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::WorldLoadChunk;
        return true;
    }

    if (source_command.command_type == command::kWorldUnloadChunk) {
        if (!command::TryParseWorldChunkPayload(
                source_command.payload,
                out_typed_command.world_chunk)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::WorldUnloadChunk;
        return true;
    }

    if (source_command.command_type == command::kGameplayCollectResource) {
        if (!command::TryParseCollectResourcePayload(
                source_command.payload,
                out_typed_command.collect_resource)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayCollectResource;
        return true;
    }

    if (source_command.command_type == command::kGameplaySpawnDrop) {
        if (!command::TryParseSpawnDropPayload(
                source_command.payload,
                out_typed_command.spawn_drop)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplaySpawnDrop;
        return true;
    }

    if (source_command.command_type == command::kGameplayPickupProbe) {
        if (!command::TryParsePickupProbePayload(
                source_command.payload,
                out_typed_command.pickup_probe)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayPickupProbe;
        return true;
    }

    if (source_command.command_type == command::kGameplayInteraction) {
        if (!command::TryParseInteractionPayload(
                source_command.payload,
                out_typed_command.interaction)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayInteraction;
        return true;
    }

    if (source_command.command_type == command::kGameplayBuildWorkbench) {
        out_typed_command.type = TypedPlayerCommandType::GameplayBuildWorkbench;
        return true;
    }

    if (source_command.command_type == command::kGameplayCraftSword) {
        out_typed_command.type = TypedPlayerCommandType::GameplayCraftSword;
        return true;
    }

    if (source_command.command_type == command::kGameplayAttackEnemy) {
        out_typed_command.type = TypedPlayerCommandType::GameplayAttackEnemy;
        return true;
    }

    if (source_command.command_type == command::kGameplayAttackBoss) {
        out_typed_command.type = TypedPlayerCommandType::GameplayAttackBoss;
        return true;
    }

    if (source_command.command_type == command::kCombatFireProjectile) {
        if (!command::TryParseFireProjectilePayload(
                source_command.payload,
                out_typed_command.fire_projectile)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::CombatFireProjectile;
        return true;
    }

    return false;
}

}  // namespace novaria::sim
