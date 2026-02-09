#include "world/snapshot_codec.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

bool TestRoundTripEncodeDecode() {
    bool passed = true;

    novaria::world::ChunkSnapshot input{
        .chunk_coord = {.x = -2, .y = 5},
        .tiles = {1, 7, 9, 65535},
    };

    novaria::wire::ByteBuffer payload;
    std::string error;
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(input, payload, error),
        "Encode should succeed.");
    passed &= Expect(!payload.empty(), "Encoded payload should not be empty.");

    novaria::world::ChunkSnapshot output{};
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot(
            novaria::wire::ByteSpan(payload.data(), payload.size()),
            output,
            error),
        "Decode should succeed.");
    passed &= Expect(output.chunk_coord.x == input.chunk_coord.x, "Decoded chunk x should match.");
    passed &= Expect(output.chunk_coord.y == input.chunk_coord.y, "Decoded chunk y should match.");
    passed &= Expect(output.tiles == input.tiles, "Decoded tiles should match.");

    return passed;
}

bool TestDecodeRejectsInvalidPayload() {
    bool passed = true;
    novaria::world::ChunkSnapshot output{};
    std::string error;

    passed &= Expect(
        !novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot({}, output, error),
        "Decode should fail when payload is empty.");
    passed &= Expect(!error.empty(), "Decode failure should provide error message.");

    passed &= Expect(
        !novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot(
            novaria::wire::ByteSpan(reinterpret_cast<const novaria::wire::Byte*>("\x01\x02"), 2),
            output,
            error),
        "Decode should fail on truncated payload.");

    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestRoundTripEncodeDecode();
    passed &= TestDecodeRejectsInvalidPayload();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_world_snapshot_codec_tests\n";
    return 0;
}
