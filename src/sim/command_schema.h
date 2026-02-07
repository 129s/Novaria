#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace novaria::sim::command {

inline constexpr std::string_view kJump = "jump";
inline constexpr std::string_view kAttack = "attack";
inline constexpr std::string_view kWorldSetTile = "world.set_tile";
inline constexpr std::string_view kWorldLoadChunk = "world.load_chunk";
inline constexpr std::string_view kWorldUnloadChunk = "world.unload_chunk";
inline constexpr std::string_view kGameplayCollectResource = "gameplay.collect_resource";
inline constexpr std::string_view kGameplaySpawnDrop = "gameplay.spawn_drop";
inline constexpr std::string_view kGameplayPickupProbe = "gameplay.pickup_probe";
inline constexpr std::string_view kGameplayInteraction = "gameplay.interaction";
inline constexpr std::string_view kGameplayBuildWorkbench = "gameplay.build_workbench";
inline constexpr std::string_view kGameplayCraftSword = "gameplay.craft_sword";
inline constexpr std::string_view kGameplayAttackEnemy = "gameplay.attack_enemy";
inline constexpr std::string_view kGameplayAttackBoss = "gameplay.attack_boss";
inline constexpr std::string_view kCombatFireProjectile = "combat.fire_projectile";

inline constexpr std::uint16_t kResourceWood = 1;
inline constexpr std::uint16_t kResourceStone = 2;
inline constexpr std::uint16_t kInteractionTypeNone = 0;
inline constexpr std::uint16_t kInteractionTypeOpenCrafting = 1;
inline constexpr std::uint16_t kInteractionTypeCraftRecipe = 2;
inline constexpr std::uint16_t kInteractionResultNone = 0;
inline constexpr std::uint16_t kInteractionResultSuccess = 1;
inline constexpr std::uint16_t kInteractionResultRejected = 2;

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

struct FireProjectilePayload final {
    int origin_tile_x = 0;
    int origin_tile_y = 0;
    int velocity_milli_x = 0;
    int velocity_milli_y = 0;
    std::uint16_t damage = 0;
    std::uint16_t lifetime_ticks = 0;
    std::uint16_t faction = 0;
};

inline bool TryParseSignedInt(std::string_view token, int& out_value) {
    if (token.empty()) {
        return false;
    }

    int parsed = 0;
    const auto [parse_end, error] =
        std::from_chars(token.data(), token.data() + token.size(), parsed);
    if (error != std::errc{} || parse_end != token.data() + token.size()) {
        return false;
    }

    out_value = parsed;
    return true;
}

inline bool TryParseMaterialId(std::string_view token, std::uint16_t& out_material_id) {
    if (token.empty()) {
        return false;
    }

    unsigned int parsed = 0;
    const auto [parse_end, error] =
        std::from_chars(token.data(), token.data() + token.size(), parsed);
    if (error != std::errc{} || parse_end != token.data() + token.size()) {
        return false;
    }
    if (parsed > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    out_material_id = static_cast<std::uint16_t>(parsed);
    return true;
}

inline bool TryParseUnsignedInt(std::string_view token, std::uint32_t& out_value) {
    if (token.empty()) {
        return false;
    }

    std::uint64_t parsed = 0;
    const auto [parse_end, error] =
        std::from_chars(token.data(), token.data() + token.size(), parsed);
    if (error != std::errc{} || parse_end != token.data() + token.size()) {
        return false;
    }
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    out_value = static_cast<std::uint32_t>(parsed);
    return true;
}

inline bool TryParseWorldSetTilePayload(
    std::string_view payload,
    WorldSetTilePayload& out_payload) {
    const auto first_separator = payload.find(',');
    if (first_separator == std::string_view::npos) {
        return false;
    }

    const auto second_separator = payload.find(',', first_separator + 1);
    if (second_separator == std::string_view::npos) {
        return false;
    }
    if (payload.find(',', second_separator + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view x_token = payload.substr(0, first_separator);
    const std::string_view y_token = payload.substr(
        first_separator + 1,
        second_separator - first_separator - 1);
    const std::string_view material_token = payload.substr(second_separator + 1);

    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    if (!TryParseSignedInt(x_token, tile_x) ||
        !TryParseSignedInt(y_token, tile_y) ||
        !TryParseMaterialId(material_token, material_id)) {
        return false;
    }

    out_payload = WorldSetTilePayload{
        .tile_x = tile_x,
        .tile_y = tile_y,
        .material_id = material_id,
    };
    return true;
}

inline bool TryParseWorldChunkPayload(std::string_view payload, WorldChunkPayload& out_payload) {
    const auto separator = payload.find(',');
    if (separator == std::string_view::npos) {
        return false;
    }
    if (payload.find(',', separator + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view x_token = payload.substr(0, separator);
    const std::string_view y_token = payload.substr(separator + 1);

    int chunk_x = 0;
    int chunk_y = 0;
    if (!TryParseSignedInt(x_token, chunk_x) ||
        !TryParseSignedInt(y_token, chunk_y)) {
        return false;
    }

    out_payload = WorldChunkPayload{
        .chunk_x = chunk_x,
        .chunk_y = chunk_y,
    };
    return true;
}

inline bool TryParseCollectResourcePayload(
    std::string_view payload,
    CollectResourcePayload& out_payload) {
    const auto separator = payload.find(',');
    if (separator == std::string_view::npos) {
        return false;
    }
    if (payload.find(',', separator + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view resource_token = payload.substr(0, separator);
    const std::string_view amount_token = payload.substr(separator + 1);

    std::uint16_t resource_id = 0;
    std::uint32_t amount = 0;
    if (!TryParseMaterialId(resource_token, resource_id) ||
        !TryParseUnsignedInt(amount_token, amount)) {
        return false;
    }
    if (amount == 0) {
        return false;
    }

    out_payload = CollectResourcePayload{
        .resource_id = resource_id,
        .amount = amount,
    };
    return true;
}

inline bool TryParseSpawnDropPayload(
    std::string_view payload,
    SpawnDropPayload& out_payload) {
    std::size_t separators[3]{};
    std::size_t search_offset = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        const std::size_t separator = payload.find(',', search_offset);
        if (separator == std::string_view::npos) {
            return false;
        }

        separators[index] = separator;
        search_offset = separator + 1;
    }
    if (payload.find(',', separators[2] + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view tile_x_token = payload.substr(0, separators[0]);
    const std::string_view tile_y_token =
        payload.substr(separators[0] + 1, separators[1] - separators[0] - 1);
    const std::string_view material_token =
        payload.substr(separators[1] + 1, separators[2] - separators[1] - 1);
    const std::string_view amount_token = payload.substr(separators[2] + 1);

    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    std::uint32_t amount = 0;
    if (!TryParseSignedInt(tile_x_token, tile_x) ||
        !TryParseSignedInt(tile_y_token, tile_y) ||
        !TryParseMaterialId(material_token, material_id) ||
        !TryParseUnsignedInt(amount_token, amount)) {
        return false;
    }
    if (amount == 0) {
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

inline bool TryParsePickupProbePayload(
    std::string_view payload,
    PickupProbePayload& out_payload) {
    const auto separator = payload.find(',');
    if (separator == std::string_view::npos) {
        return false;
    }
    if (payload.find(',', separator + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view x_token = payload.substr(0, separator);
    const std::string_view y_token = payload.substr(separator + 1);

    int tile_x = 0;
    int tile_y = 0;
    if (!TryParseSignedInt(x_token, tile_x) ||
        !TryParseSignedInt(y_token, tile_y)) {
        return false;
    }

    out_payload = PickupProbePayload{
        .tile_x = tile_x,
        .tile_y = tile_y,
    };
    return true;
}

inline bool TryParseInteractionPayload(
    std::string_view payload,
    InteractionPayload& out_payload) {
    std::size_t separators[4]{};
    std::size_t search_offset = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const std::size_t separator = payload.find(',', search_offset);
        if (separator == std::string_view::npos) {
            return false;
        }

        separators[index] = separator;
        search_offset = separator + 1;
    }
    if (payload.find(',', separators[3] + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view interaction_type_token = payload.substr(0, separators[0]);
    const std::string_view target_x_token =
        payload.substr(separators[0] + 1, separators[1] - separators[0] - 1);
    const std::string_view target_y_token =
        payload.substr(separators[1] + 1, separators[2] - separators[1] - 1);
    const std::string_view target_material_token =
        payload.substr(separators[2] + 1, separators[3] - separators[2] - 1);
    const std::string_view result_code_token = payload.substr(separators[3] + 1);

    std::uint16_t interaction_type = 0;
    int target_tile_x = 0;
    int target_tile_y = 0;
    std::uint16_t target_material_id = 0;
    std::uint16_t result_code = 0;
    if (!TryParseMaterialId(interaction_type_token, interaction_type) ||
        !TryParseSignedInt(target_x_token, target_tile_x) ||
        !TryParseSignedInt(target_y_token, target_tile_y) ||
        !TryParseMaterialId(target_material_token, target_material_id) ||
        !TryParseMaterialId(result_code_token, result_code)) {
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

inline bool TryParseFireProjectilePayload(
    std::string_view payload,
    FireProjectilePayload& out_payload) {
    std::size_t separators[6]{};
    std::size_t search_offset = 0;
    for (std::size_t index = 0; index < 6; ++index) {
        const std::size_t separator = payload.find(',', search_offset);
        if (separator == std::string_view::npos) {
            return false;
        }

        separators[index] = separator;
        search_offset = separator + 1;
    }
    if (payload.find(',', separators[5] + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view tokens[7]{
        payload.substr(0, separators[0]),
        payload.substr(separators[0] + 1, separators[1] - separators[0] - 1),
        payload.substr(separators[1] + 1, separators[2] - separators[1] - 1),
        payload.substr(separators[2] + 1, separators[3] - separators[2] - 1),
        payload.substr(separators[3] + 1, separators[4] - separators[3] - 1),
        payload.substr(separators[4] + 1, separators[5] - separators[4] - 1),
        payload.substr(separators[5] + 1),
    };

    int origin_tile_x = 0;
    int origin_tile_y = 0;
    int velocity_milli_x = 0;
    int velocity_milli_y = 0;
    std::uint16_t damage = 0;
    std::uint16_t lifetime_ticks = 0;
    std::uint16_t faction = 0;
    if (!TryParseSignedInt(tokens[0], origin_tile_x) ||
        !TryParseSignedInt(tokens[1], origin_tile_y) ||
        !TryParseSignedInt(tokens[2], velocity_milli_x) ||
        !TryParseSignedInt(tokens[3], velocity_milli_y) ||
        !TryParseMaterialId(tokens[4], damage) ||
        !TryParseMaterialId(tokens[5], lifetime_ticks) ||
        !TryParseMaterialId(tokens[6], faction)) {
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

inline std::string BuildWorldSetTilePayload(int tile_x, int tile_y, std::uint16_t material_id) {
    return std::to_string(tile_x) + "," + std::to_string(tile_y) + "," + std::to_string(material_id);
}

inline std::string BuildWorldChunkPayload(int chunk_x, int chunk_y) {
    return std::to_string(chunk_x) + "," + std::to_string(chunk_y);
}

inline std::string BuildCollectResourcePayload(std::uint16_t resource_id, std::uint32_t amount) {
    return std::to_string(resource_id) + "," + std::to_string(amount);
}

inline std::string BuildSpawnDropPayload(
    int tile_x,
    int tile_y,
    std::uint16_t material_id,
    std::uint32_t amount) {
    return std::to_string(tile_x) + "," + std::to_string(tile_y) + "," +
        std::to_string(material_id) + "," + std::to_string(amount);
}

inline std::string BuildPickupProbePayload(int tile_x, int tile_y) {
    return std::to_string(tile_x) + "," + std::to_string(tile_y);
}

inline std::string BuildInteractionPayload(
    std::uint16_t interaction_type,
    int target_tile_x,
    int target_tile_y,
    std::uint16_t target_material_id,
    std::uint16_t result_code) {
    return std::to_string(interaction_type) + "," + std::to_string(target_tile_x) + "," +
        std::to_string(target_tile_y) + "," + std::to_string(target_material_id) + "," +
        std::to_string(result_code);
}

inline std::string BuildFireProjectilePayload(
    int origin_tile_x,
    int origin_tile_y,
    int velocity_milli_x,
    int velocity_milli_y,
    std::uint16_t damage,
    std::uint16_t lifetime_ticks,
    std::uint16_t faction) {
    return std::to_string(origin_tile_x) + "," + std::to_string(origin_tile_y) + "," +
        std::to_string(velocity_milli_x) + "," + std::to_string(velocity_milli_y) + "," +
        std::to_string(damage) + "," + std::to_string(lifetime_ticks) + "," +
        std::to_string(faction);
}

}  // namespace novaria::sim::command
