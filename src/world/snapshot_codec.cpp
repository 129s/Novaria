#include "world/snapshot_codec.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace novaria::world {
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

}  // namespace

bool WorldSnapshotCodec::EncodeChunkSnapshot(
    const ChunkSnapshot& snapshot,
    wire::ByteBuffer& out_payload,
    std::string& out_error) {
    if (snapshot.tiles.empty()) {
        out_error = "snapshot tiles cannot be empty";
        return false;
    }

    const std::size_t tile_count = snapshot.tiles.size();
    if (tile_count > (std::numeric_limits<std::size_t>::max() / 2)) {
        out_error = "snapshot tile count overflow";
        return false;
    }

    std::vector<wire::Byte> tiles_bytes;
    tiles_bytes.resize(tile_count * 2);
    for (std::size_t index = 0; index < tile_count; ++index) {
        const std::uint16_t material_id = snapshot.tiles[index];
        tiles_bytes[index * 2] = static_cast<wire::Byte>(material_id & 0xFF);
        tiles_bytes[index * 2 + 1] = static_cast<wire::Byte>((material_id >> 8) & 0xFF);
    }

    wire::ByteWriter writer;
    writer.WriteVarInt(snapshot.chunk_coord.x);
    writer.WriteVarInt(snapshot.chunk_coord.y);
    writer.WriteVarUInt(tile_count);
    writer.WriteBytes(wire::ByteSpan(tiles_bytes.data(), tiles_bytes.size()));

    out_payload = writer.TakeBuffer();
    out_error.clear();
    return true;
}

bool WorldSnapshotCodec::DecodeChunkSnapshot(
    wire::ByteSpan payload,
    ChunkSnapshot& out_snapshot,
    std::string& out_error) {
    if (payload.empty()) {
        out_error = "payload is empty";
        return false;
    }

    wire::ByteReader reader(payload);
    int chunk_x = 0;
    int chunk_y = 0;
    std::uint64_t tile_count_u64 = 0;
    if (!TryReadVarInt32(reader, chunk_x) ||
        !TryReadVarInt32(reader, chunk_y) ||
        !reader.ReadVarUInt(tile_count_u64)) {
        out_error = "invalid chunk header fields";
        return false;
    }
    if (tile_count_u64 == 0) {
        out_error = "tile_count cannot be zero";
        return false;
    }
    if (tile_count_u64 > std::numeric_limits<std::size_t>::max()) {
        out_error = "tile_count overflow";
        return false;
    }

    wire::ByteSpan tiles_bytes{};
    if (!reader.ReadBytes(tiles_bytes) || !reader.IsFullyConsumed()) {
        out_error = "invalid tiles bytes field";
        return false;
    }

    const std::size_t tile_count = static_cast<std::size_t>(tile_count_u64);
    if (tile_count > (std::numeric_limits<std::size_t>::max() / 2)) {
        out_error = "tile_count overflow";
        return false;
    }
    if (tiles_bytes.size() != tile_count * 2) {
        out_error = "tiles bytes length does not match tile_count";
        return false;
    }

    ChunkSnapshot snapshot{};
    snapshot.chunk_coord = ChunkCoord{
        .x = chunk_x,
        .y = chunk_y,
    };
    snapshot.tiles.resize(tile_count);
    for (std::size_t index = 0; index < tile_count; ++index) {
        const std::uint16_t low = tiles_bytes[index * 2];
        const std::uint16_t high = tiles_bytes[index * 2 + 1];
        snapshot.tiles[index] = static_cast<std::uint16_t>(low | (high << 8));
    }

    out_snapshot = std::move(snapshot);
    out_error.clear();
    return true;
}

}  // namespace novaria::world

