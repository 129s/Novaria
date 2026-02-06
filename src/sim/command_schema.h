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

struct WorldSetTilePayload final {
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
};

struct WorldChunkPayload final {
    int chunk_x = 0;
    int chunk_y = 0;
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

inline std::string BuildWorldSetTilePayload(int tile_x, int tile_y, std::uint16_t material_id) {
    return std::to_string(tile_x) + "," + std::to_string(tile_y) + "," + std::to_string(material_id);
}

inline std::string BuildWorldChunkPayload(int chunk_x, int chunk_y) {
    return std::to_string(chunk_x) + "," + std::to_string(chunk_y);
}

}  // namespace novaria::sim::command
