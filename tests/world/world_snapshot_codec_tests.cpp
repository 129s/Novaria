#include "world/snapshot_codec.h"

#include <iostream>
#include <string>
#include <string_view>

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

    std::string payload;
    std::string error;
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(input, payload, error),
        "Encode should succeed.");
    passed &= Expect(!payload.empty(), "Encoded payload should not be empty.");

    novaria::world::ChunkSnapshot output{};
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot(payload, output, error),
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
        !novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot("0,0,2,1", output, error),
        "Decode should fail when payload tile count mismatches.");
    passed &= Expect(!error.empty(), "Decode failure should provide error message.");

    passed &= Expect(
        !novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot("a,b,c", output, error),
        "Decode should fail on non-numeric fields.");

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
