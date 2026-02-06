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
inline constexpr std::string_view kGameplayBuildWorkbench = "gameplay.build_workbench";
inline constexpr std::string_view kGameplayCraftSword = "gameplay.craft_sword";
inline constexpr std::string_view kGameplayAttackEnemy = "gameplay.attack_enemy";
inline constexpr std::string_view kGameplayAttackBoss = "gameplay.attack_boss";

inline constexpr std::uint16_t kResourceWood = 1;
inline constexpr std::uint16_t kResourceStone = 2;

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

inline std::string BuildWorldSetTilePayload(int tile_x, int tile_y, std::uint16_t material_id) {
    return std::to_string(tile_x) + "," + std::to_string(tile_y) + "," + std::to_string(material_id);
}

inline std::string BuildWorldChunkPayload(int chunk_x, int chunk_y) {
    return std::to_string(chunk_x) + "," + std::to_string(chunk_y);
}

inline std::string BuildCollectResourcePayload(std::uint16_t resource_id, std::uint32_t amount) {
    return std::to_string(resource_id) + "," + std::to_string(amount);
}

}  // namespace novaria::sim::command
