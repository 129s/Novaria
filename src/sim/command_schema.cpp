#include "sim/command_schema.h"

#include <limits>

namespace novaria::sim::command {
namespace {

bool TryReadVarInt32(wire::ByteReader& reader, int& out_value) {
    std::int64_t parsed = 0;
    if (!reader.ReadVarInt(parsed)) {
        return false;
    }
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    out_value = static_cast<int>(parsed);
    return true;
}

bool TryReadVarUInt16(wire::ByteReader& reader, std::uint16_t& out_value) {
    std::uint64_t parsed = 0;
    if (!reader.ReadVarUInt(parsed) || parsed > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    out_value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool TryReadVarUInt32(wire::ByteReader& reader, std::uint32_t& out_value) {
    std::uint64_t parsed = 0;
    if (!reader.ReadVarUInt(parsed) || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out_value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool EnsureFullyConsumed(const wire::ByteReader& reader) {
    return reader.IsFullyConsumed();
}

}  // namespace

const char* CommandName(std::uint32_t command_id) {
    switch (command_id) {
        case kJump:
            return "jump";
        case kAttack:
            return "attack";
        case kPlayerMotionInput:
            return "player.motion_input";
        case kWorldSetTile:
            return "world.set_tile";
        case kWorldLoadChunk:
            return "world.load_chunk";
        case kWorldUnloadChunk:
            return "world.unload_chunk";
        case kGameplayCollectResource:
            return "gameplay.collect_resource";
        case kGameplaySpawnDrop:
            return "gameplay.spawn_drop";
        case kGameplayPickupProbe:
            return "gameplay.pickup_probe";
        case kGameplayInteraction:
            return "gameplay.interaction";
        case kGameplayActionPrimary:
            return "gameplay.action_primary";
        case kGameplayCraftRecipe:
            return "gameplay.craft_recipe";
        case kGameplayAttackEnemy:
            return "gameplay.attack_enemy";
        case kGameplayAttackBoss:
            return "gameplay.attack_boss";
        case kCombatFireProjectile:
            return "combat.fire_projectile";
    }

    return "unknown";
}

wire::ByteBuffer EncodeWorldSetTilePayload(const WorldSetTilePayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarInt(payload.tile_x);
    writer.WriteVarInt(payload.tile_y);
    writer.WriteVarUInt(payload.material_id);
    return writer.TakeBuffer();
}

bool TryDecodeWorldSetTilePayload(wire::ByteSpan payload, WorldSetTilePayload& out_payload) {
    wire::ByteReader reader(payload);
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    if (!TryReadVarInt32(reader, tile_x) ||
        !TryReadVarInt32(reader, tile_y) ||
        !TryReadVarUInt16(reader, material_id) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    out_payload = WorldSetTilePayload{
        .tile_x = tile_x,
        .tile_y = tile_y,
        .material_id = material_id,
    };
    return true;
}

wire::ByteBuffer EncodeWorldChunkPayload(const WorldChunkPayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarInt(payload.chunk_x);
    writer.WriteVarInt(payload.chunk_y);
    return writer.TakeBuffer();
}

bool TryDecodeWorldChunkPayload(wire::ByteSpan payload, WorldChunkPayload& out_payload) {
    wire::ByteReader reader(payload);
    int chunk_x = 0;
    int chunk_y = 0;
    if (!TryReadVarInt32(reader, chunk_x) ||
        !TryReadVarInt32(reader, chunk_y) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    out_payload = WorldChunkPayload{
        .chunk_x = chunk_x,
        .chunk_y = chunk_y,
    };
    return true;
}

wire::ByteBuffer EncodeCollectResourcePayload(const CollectResourcePayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarUInt(payload.resource_id);
    writer.WriteVarUInt(payload.amount);
    return writer.TakeBuffer();
}

bool TryDecodeCollectResourcePayload(wire::ByteSpan payload, CollectResourcePayload& out_payload) {
    wire::ByteReader reader(payload);
    std::uint16_t resource_id = 0;
    std::uint32_t amount = 0;
    if (!TryReadVarUInt16(reader, resource_id) ||
        !TryReadVarUInt32(reader, amount) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    if (resource_id == 0 || amount == 0) {
        return false;
    }
    out_payload = CollectResourcePayload{
        .resource_id = resource_id,
        .amount = amount,
    };
    return true;
}

wire::ByteBuffer EncodeSpawnDropPayload(const SpawnDropPayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarInt(payload.tile_x);
    writer.WriteVarInt(payload.tile_y);
    writer.WriteVarUInt(payload.material_id);
    writer.WriteVarUInt(payload.amount);
    return writer.TakeBuffer();
}

bool TryDecodeSpawnDropPayload(wire::ByteSpan payload, SpawnDropPayload& out_payload) {
    wire::ByteReader reader(payload);
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    std::uint32_t amount = 0;
    if (!TryReadVarInt32(reader, tile_x) ||
        !TryReadVarInt32(reader, tile_y) ||
        !TryReadVarUInt16(reader, material_id) ||
        !TryReadVarUInt32(reader, amount) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    if (material_id == 0 || amount == 0) {
        return false;
    }
    out_payload = SpawnDropPayload{
        .tile_x = tile_x,
        .tile_y = tile_y,
        .material_id = material_id,
        .amount = amount,
    };
    return true;
}

wire::ByteBuffer EncodePickupProbePayload(const PickupProbePayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarInt(payload.tile_x);
    writer.WriteVarInt(payload.tile_y);
    return writer.TakeBuffer();
}

bool TryDecodePickupProbePayload(wire::ByteSpan payload, PickupProbePayload& out_payload) {
    wire::ByteReader reader(payload);
    int tile_x = 0;
    int tile_y = 0;
    if (!TryReadVarInt32(reader, tile_x) ||
        !TryReadVarInt32(reader, tile_y) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    out_payload = PickupProbePayload{
        .tile_x = tile_x,
        .tile_y = tile_y,
    };
    return true;
}

wire::ByteBuffer EncodeInteractionPayload(const InteractionPayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarUInt(payload.interaction_type);
    writer.WriteVarInt(payload.target_tile_x);
    writer.WriteVarInt(payload.target_tile_y);
    writer.WriteVarUInt(payload.target_material_id);
    writer.WriteVarUInt(payload.result_code);
    return writer.TakeBuffer();
}

bool TryDecodeInteractionPayload(wire::ByteSpan payload, InteractionPayload& out_payload) {
    wire::ByteReader reader(payload);
    std::uint16_t interaction_type = 0;
    int target_tile_x = 0;
    int target_tile_y = 0;
    std::uint16_t target_material_id = 0;
    std::uint16_t result_code = 0;
    if (!TryReadVarUInt16(reader, interaction_type) ||
        !TryReadVarInt32(reader, target_tile_x) ||
        !TryReadVarInt32(reader, target_tile_y) ||
        !TryReadVarUInt16(reader, target_material_id) ||
        !TryReadVarUInt16(reader, result_code) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    out_payload = InteractionPayload{
        .interaction_type = interaction_type,
        .target_tile_x = target_tile_x,
        .target_tile_y = target_tile_y,
        .target_material_id = target_material_id,
        .result_code = result_code,
    };
    return true;
}

wire::ByteBuffer EncodePlayerMotionInputPayload(const PlayerMotionInputPayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarInt(payload.move_axis_milli);
    writer.WriteVarUInt(payload.input_flags);
    return writer.TakeBuffer();
}

bool TryDecodePlayerMotionInputPayload(wire::ByteSpan payload, PlayerMotionInputPayload& out_payload) {
    wire::ByteReader reader(payload);
    int move_axis_milli = 0;
    std::uint64_t input_flags = 0;
    if (!TryReadVarInt32(reader, move_axis_milli) ||
        !reader.ReadVarUInt(input_flags) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    if (move_axis_milli < -1000 || move_axis_milli > 1000) {
        return false;
    }
    if (input_flags > 255) {
        return false;
    }

    const std::uint8_t flags_u8 = static_cast<std::uint8_t>(input_flags);
    if ((flags_u8 & ~kMotionInputFlagJumpPressed) != 0) {
        return false;
    }

    out_payload = PlayerMotionInputPayload{
        .move_axis_milli = move_axis_milli,
        .input_flags = flags_u8,
    };
    return true;
}

wire::ByteBuffer EncodeActionPrimaryPayload(const ActionPrimaryPayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarInt(payload.target_tile_x);
    writer.WriteVarInt(payload.target_tile_y);
    writer.WriteVarUInt(payload.hotbar_row);
    writer.WriteVarUInt(payload.hotbar_slot);
    return writer.TakeBuffer();
}

bool TryDecodeActionPrimaryPayload(wire::ByteSpan payload, ActionPrimaryPayload& out_payload) {
    wire::ByteReader reader(payload);
    int target_tile_x = 0;
    int target_tile_y = 0;
    std::uint64_t hotbar_row = 0;
    std::uint64_t hotbar_slot = 0;
    if (!TryReadVarInt32(reader, target_tile_x) ||
        !TryReadVarInt32(reader, target_tile_y) ||
        !reader.ReadVarUInt(hotbar_row) ||
        !reader.ReadVarUInt(hotbar_slot) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    if (hotbar_row > 255 || hotbar_slot > 255) {
        return false;
    }
    out_payload = ActionPrimaryPayload{
        .target_tile_x = target_tile_x,
        .target_tile_y = target_tile_y,
        .hotbar_row = static_cast<std::uint8_t>(hotbar_row),
        .hotbar_slot = static_cast<std::uint8_t>(hotbar_slot),
    };
    return true;
}

wire::ByteBuffer EncodeCraftRecipePayload(const CraftRecipePayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarUInt(payload.recipe_index);
    return writer.TakeBuffer();
}

bool TryDecodeCraftRecipePayload(wire::ByteSpan payload, CraftRecipePayload& out_payload) {
    wire::ByteReader reader(payload);
    std::uint64_t recipe_index = 0;
    if (!reader.ReadVarUInt(recipe_index) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    if (recipe_index > 255) {
        return false;
    }
    out_payload = CraftRecipePayload{
        .recipe_index = static_cast<std::uint8_t>(recipe_index),
    };
    return true;
}

wire::ByteBuffer EncodeFireProjectilePayload(const FireProjectilePayload& payload) {
    wire::ByteWriter writer;
    writer.WriteVarInt(payload.origin_tile_x);
    writer.WriteVarInt(payload.origin_tile_y);
    writer.WriteVarInt(payload.velocity_milli_x);
    writer.WriteVarInt(payload.velocity_milli_y);
    writer.WriteVarUInt(payload.damage);
    writer.WriteVarUInt(payload.lifetime_ticks);
    writer.WriteVarUInt(payload.faction);
    return writer.TakeBuffer();
}

bool TryDecodeFireProjectilePayload(wire::ByteSpan payload, FireProjectilePayload& out_payload) {
    wire::ByteReader reader(payload);
    int origin_tile_x = 0;
    int origin_tile_y = 0;
    int velocity_milli_x = 0;
    int velocity_milli_y = 0;
    std::uint16_t damage = 0;
    std::uint16_t lifetime_ticks = 0;
    std::uint16_t faction = 0;
    if (!TryReadVarInt32(reader, origin_tile_x) ||
        !TryReadVarInt32(reader, origin_tile_y) ||
        !TryReadVarInt32(reader, velocity_milli_x) ||
        !TryReadVarInt32(reader, velocity_milli_y) ||
        !TryReadVarUInt16(reader, damage) ||
        !TryReadVarUInt16(reader, lifetime_ticks) ||
        !TryReadVarUInt16(reader, faction) ||
        !EnsureFullyConsumed(reader)) {
        return false;
    }
    if (damage == 0 || lifetime_ticks == 0 || faction == 0) {
        return false;
    }
    out_payload = FireProjectilePayload{
        .origin_tile_x = origin_tile_x,
        .origin_tile_y = origin_tile_y,
        .velocity_milli_x = velocity_milli_x,
        .velocity_milli_y = velocity_milli_y,
        .damage = damage,
        .lifetime_ticks = lifetime_ticks,
        .faction = faction,
    };
    return true;
}

}  // namespace novaria::sim::command
