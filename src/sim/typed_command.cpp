#include "sim/typed_command.h"

namespace novaria::sim {

bool TryDecodePlayerCommand(
    const net::PlayerCommand& source_command,
    TypedPlayerCommand& out_typed_command) {
    out_typed_command = {};

    if (source_command.command_id == command::kJump) {
        if (!source_command.payload.empty()) {
            return false;
        }
        out_typed_command.type = TypedPlayerCommandType::Jump;
        return true;
    }

    if (source_command.command_id == command::kAttack) {
        if (!source_command.payload.empty()) {
            return false;
        }
        out_typed_command.type = TypedPlayerCommandType::Attack;
        return true;
    }

    if (source_command.command_id == command::kPlayerMotionInput) {
        if (!command::TryDecodePlayerMotionInputPayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.player_motion_input)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::PlayerMotionInput;
        return true;
    }

    if (source_command.command_id == command::kWorldSetTile) {
        if (!command::TryDecodeWorldSetTilePayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.world_set_tile)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::WorldSetTile;
        return true;
    }

    if (source_command.command_id == command::kWorldLoadChunk) {
        if (!command::TryDecodeWorldChunkPayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.world_chunk)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::WorldLoadChunk;
        return true;
    }

    if (source_command.command_id == command::kWorldUnloadChunk) {
        if (!command::TryDecodeWorldChunkPayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.world_chunk)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::WorldUnloadChunk;
        return true;
    }

    if (source_command.command_id == command::kGameplayCollectResource) {
        if (!command::TryDecodeCollectResourcePayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.collect_resource)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayCollectResource;
        return true;
    }

    if (source_command.command_id == command::kGameplaySpawnDrop) {
        if (!command::TryDecodeSpawnDropPayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.spawn_drop)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplaySpawnDrop;
        return true;
    }

    if (source_command.command_id == command::kGameplayPickupProbe) {
        if (!command::TryDecodePickupProbePayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.pickup_probe)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayPickupProbe;
        return true;
    }

    if (source_command.command_id == command::kGameplayInteraction) {
        if (!command::TryDecodeInteractionPayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.interaction)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayInteraction;
        return true;
    }

    if (source_command.command_id == command::kGameplayActionPrimary) {
        if (!command::TryDecodeActionPrimaryPayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.action_primary)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayActionPrimary;
        return true;
    }

    if (source_command.command_id == command::kGameplayCraftRecipe) {
        if (!command::TryDecodeCraftRecipePayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.craft_recipe)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::GameplayCraftRecipe;
        return true;
    }

    if (source_command.command_id == command::kGameplayAttackEnemy) {
        if (!source_command.payload.empty()) {
            return false;
        }
        out_typed_command.type = TypedPlayerCommandType::GameplayAttackEnemy;
        return true;
    }

    if (source_command.command_id == command::kGameplayAttackBoss) {
        if (!source_command.payload.empty()) {
            return false;
        }
        out_typed_command.type = TypedPlayerCommandType::GameplayAttackBoss;
        return true;
    }

    if (source_command.command_id == command::kCombatFireProjectile) {
        if (!command::TryDecodeFireProjectilePayload(
                wire::ByteSpan(source_command.payload.data(), source_command.payload.size()),
                out_typed_command.fire_projectile)) {
            return false;
        }

        out_typed_command.type = TypedPlayerCommandType::CombatFireProjectile;
        return true;
    }

    return false;
}

}  // namespace novaria::sim
