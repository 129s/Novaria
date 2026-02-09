#pragma once

#include "wire/byte_io.h"

#include <cstdint>
#include <string>

namespace novaria::wire {

inline constexpr std::uint8_t kWireVersionV1 = 1;

enum class MessageKind : std::uint8_t {
    Control = 1,
    Command = 2,
    ChunkSnapshot = 3,
    ChunkSnapshotBatch = 4,
};

const char* MessageKindName(MessageKind kind);

struct EnvelopeView final {
    MessageKind kind = MessageKind::Control;
    ByteSpan payload{};
};

bool TryDecodeEnvelopeV1(ByteSpan datagram, EnvelopeView& out_envelope, std::string& out_error);

void EncodeEnvelopeV1(MessageKind kind, ByteSpan payload, ByteBuffer& out_datagram);

}  // namespace novaria::wire

