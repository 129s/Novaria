#pragma once

#include "world/world_service.h"

#include <string>
#include <string_view>

namespace novaria::world {

class WorldSnapshotCodec final {
public:
    static bool EncodeChunkSnapshot(
        const ChunkSnapshot& snapshot,
        std::string& out_payload,
        std::string& out_error);
    static bool DecodeChunkSnapshot(
        std::string_view payload,
        ChunkSnapshot& out_snapshot,
        std::string& out_error);
};

}  // namespace novaria::world
