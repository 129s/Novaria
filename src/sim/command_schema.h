#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace novaria::sim::command {

inline constexpr std::string_view kJump = "jump";
inline constexpr std::string_view kAttack = "attack";
inline constexpr std::string_view kWorldSetTile = "world.set_tile";
inline constexpr std::string_view kWorldLoadChunk = "world.load_chunk";
inline constexpr std::string_view kWorldUnloadChunk = "world.unload_chunk";

inline std::string BuildWorldSetTilePayload(int tile_x, int tile_y, std::uint16_t material_id) {
    return std::to_string(tile_x) + "," + std::to_string(tile_y) + "," + std::to_string(material_id);
}

inline std::string BuildWorldChunkPayload(int chunk_x, int chunk_y) {
    return std::to_string(chunk_x) + "," + std::to_string(chunk_y);
}

}  // namespace novaria::sim::command
