#include "world/snapshot_codec.h"

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace novaria::world {
namespace {

bool ParseSignedInt32(const std::string& token, int& out_value) {
    try {
        size_t consumed = 0;
        const long long parsed = std::stoll(token, &consumed);
        if (consumed != token.size()) {
            return false;
        }
        if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        out_value = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseUnsignedSize(const std::string& token, std::size_t& out_value) {
    try {
        size_t consumed = 0;
        const auto parsed = std::stoull(token, &consumed);
        if (consumed != token.size()) {
            return false;
        }
        out_value = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseUint16(const std::string& token, std::uint16_t& out_value) {
    try {
        size_t consumed = 0;
        const auto parsed = std::stoull(token, &consumed);
        if (consumed != token.size() || parsed > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        out_value = static_cast<std::uint16_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> SplitByComma(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(text.size());

    for (char ch : text) {
        if (ch == ',') {
            tokens.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    tokens.push_back(current);
    return tokens;
}

}  // namespace

bool WorldSnapshotCodec::EncodeChunkSnapshot(
    const ChunkSnapshot& snapshot,
    std::string& out_payload,
    std::string& out_error) {
    if (snapshot.tiles.empty()) {
        out_error = "snapshot tiles cannot be empty";
        return false;
    }

    std::ostringstream stream;
    stream << snapshot.chunk_coord.x << ',' << snapshot.chunk_coord.y << ',' << snapshot.tiles.size();
    for (const std::uint16_t material : snapshot.tiles) {
        stream << ',' << material;
    }

    out_payload = stream.str();
    out_error.clear();
    return true;
}

bool WorldSnapshotCodec::DecodeChunkSnapshot(
    std::string_view payload,
    ChunkSnapshot& out_snapshot,
    std::string& out_error) {
    if (payload.empty()) {
        out_error = "payload is empty";
        return false;
    }

    const auto tokens = SplitByComma(payload);
    if (tokens.size() < 3) {
        out_error = "payload has insufficient fields";
        return false;
    }

    int chunk_x = 0;
    int chunk_y = 0;
    std::size_t tile_count = 0;
    if (!ParseSignedInt32(tokens[0], chunk_x) || !ParseSignedInt32(tokens[1], chunk_y)) {
        out_error = "invalid chunk coordinate fields";
        return false;
    }
    if (!ParseUnsignedSize(tokens[2], tile_count)) {
        out_error = "invalid tile count field";
        return false;
    }

    if (tokens.size() != 3 + tile_count) {
        out_error = "tile count does not match payload length";
        return false;
    }

    ChunkSnapshot snapshot{};
    snapshot.chunk_coord = ChunkCoord{
        .x = chunk_x,
        .y = chunk_y,
    };
    snapshot.tiles.reserve(tile_count);

    for (std::size_t index = 0; index < tile_count; ++index) {
        std::uint16_t material = 0;
        if (!ParseUint16(tokens[3 + index], material)) {
            out_error = "invalid tile material field";
            return false;
        }
        snapshot.tiles.push_back(material);
    }

    out_snapshot = std::move(snapshot);
    out_error.clear();
    return true;
}

}  // namespace novaria::world
