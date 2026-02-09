#pragma once

#include "wire/byte_io.h"

#include <cstdint>
#include <string_view>

namespace novaria::sim::command {

inline constexpr std::uint32_t kJump = 1;
inline constexpr std::uint32_t kAttack = 2;
inline constexpr std::uint32_t kPlayerMotionInput = 3;

inline constexpr std::uint32_t kWorldSetTile = 10;
inline constexpr std::uint32_t kWorldLoadChunk = 11;
inline constexpr std::uint32_t kWorldUnloadChunk = 12;

inline constexpr std::uint32_t kGameplayCollectResource = 20;
inline constexpr std::uint32_t kGameplaySpawnDrop = 21;
inline constexpr std::uint32_t kGameplayPickupProbe = 22;
inline constexpr std::uint32_t kGameplayInteraction = 23;
inline constexpr std::uint32_t kGameplayActionPrimary = 24;
inline constexpr std::uint32_t kGameplayCraftRecipe = 25;
inline constexpr std::uint32_t kGameplayAttackEnemy = 26;
inline constexpr std::uint32_t kGameplayAttackBoss = 27;

inline constexpr std::uint32_t kCombatFireProjectile = 30;

const char* CommandName(std::uint32_t command_id);

inline constexpr std::uint16_t kResourceWood = 1;
inline constexpr std::uint16_t kResourceStone = 2;
inline constexpr std::uint16_t kInteractionTypeNone = 0;
inline constexpr std::uint16_t kInteractionTypeOpenCrafting = 1;
inline constexpr std::uint16_t kInteractionTypeCraftRecipe = 2;
inline constexpr std::uint16_t kInteractionResultNone = 0;
inline constexpr std::uint16_t kInteractionResultSuccess = 1;
inline constexpr std::uint16_t kInteractionResultRejected = 2;

inline constexpr std::uint8_t kMotionInputFlagJumpPressed = 1;

struct WorldSetTilePayload final {
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
};

struct WorldChunkPayload final {
    int chunk_x = 0;
    int chunk_y = 0;
};

struct CollectResourcePayload final {
    std::uint16_t resource_id = 0;
    std::uint32_t amount = 0;
};

struct SpawnDropPayload final {
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    std::uint32_t amount = 0;
};

struct PickupProbePayload final {
    int tile_x = 0;
    int tile_y = 0;
};

struct InteractionPayload final {
    std::uint16_t interaction_type = 0;
    int target_tile_x = 0;
    int target_tile_y = 0;
    std::uint16_t target_material_id = 0;
    std::uint16_t result_code = 0;
};

struct PlayerMotionInputPayload final {
    int move_axis_milli = 0;
    std::uint8_t input_flags = 0;
};

struct ActionPrimaryPayload final {
    int target_tile_x = 0;
    int target_tile_y = 0;
    std::uint8_t hotbar_row = 0;
    std::uint8_t hotbar_slot = 0;
};

struct CraftRecipePayload final {
    std::uint8_t recipe_index = 0;
};

struct FireProjectilePayload final {
    int origin_tile_x = 0;
    int origin_tile_y = 0;
    int velocity_milli_x = 0;
    int velocity_milli_y = 0;
    std::uint16_t damage = 0;
    std::uint16_t lifetime_ticks = 0;
    std::uint16_t faction = 0;
};

wire::ByteBuffer EncodeWorldSetTilePayload(const WorldSetTilePayload& payload);
bool TryDecodeWorldSetTilePayload(wire::ByteSpan payload, WorldSetTilePayload& out_payload);

wire::ByteBuffer EncodeWorldChunkPayload(const WorldChunkPayload& payload);
bool TryDecodeWorldChunkPayload(wire::ByteSpan payload, WorldChunkPayload& out_payload);

wire::ByteBuffer EncodeCollectResourcePayload(const CollectResourcePayload& payload);
bool TryDecodeCollectResourcePayload(wire::ByteSpan payload, CollectResourcePayload& out_payload);

wire::ByteBuffer EncodeSpawnDropPayload(const SpawnDropPayload& payload);
bool TryDecodeSpawnDropPayload(wire::ByteSpan payload, SpawnDropPayload& out_payload);

wire::ByteBuffer EncodePickupProbePayload(const PickupProbePayload& payload);
bool TryDecodePickupProbePayload(wire::ByteSpan payload, PickupProbePayload& out_payload);

wire::ByteBuffer EncodeInteractionPayload(const InteractionPayload& payload);
bool TryDecodeInteractionPayload(wire::ByteSpan payload, InteractionPayload& out_payload);

wire::ByteBuffer EncodePlayerMotionInputPayload(const PlayerMotionInputPayload& payload);
bool TryDecodePlayerMotionInputPayload(wire::ByteSpan payload, PlayerMotionInputPayload& out_payload);

wire::ByteBuffer EncodeActionPrimaryPayload(const ActionPrimaryPayload& payload);
bool TryDecodeActionPrimaryPayload(wire::ByteSpan payload, ActionPrimaryPayload& out_payload);

wire::ByteBuffer EncodeCraftRecipePayload(const CraftRecipePayload& payload);
bool TryDecodeCraftRecipePayload(wire::ByteSpan payload, CraftRecipePayload& out_payload);

wire::ByteBuffer EncodeFireProjectilePayload(const FireProjectilePayload& payload);
bool TryDecodeFireProjectilePayload(wire::ByteSpan payload, FireProjectilePayload& out_payload);

}  // namespace novaria::sim::command
