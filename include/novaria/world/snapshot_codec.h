#pragma once

#include "world/world_service.h"
#include "wire/byte_io.h"

#include <string>

namespace novaria::world {

class WorldSnapshotCodec final {
public:
    static bool EncodeChunkSnapshot(
        const ChunkSnapshot& snapshot,
        wire::ByteBuffer& out_payload,
        std::string& out_error);
    static bool DecodeChunkSnapshot(
        wire::ByteSpan payload,
        ChunkSnapshot& out_snapshot,
        std::string& out_error);
};

}  // namespace novaria::world
