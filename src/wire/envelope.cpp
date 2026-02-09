#include "wire/envelope.h"

namespace novaria::wire {

const char* MessageKindName(MessageKind kind) {
    switch (kind) {
        case MessageKind::Control:
            return "control";
        case MessageKind::Command:
            return "command";
        case MessageKind::ChunkSnapshot:
            return "chunk_snapshot";
        case MessageKind::ChunkSnapshotBatch:
            return "chunk_snapshot_batch";
    }

    return "unknown";
}

bool TryDecodeEnvelopeV1(ByteSpan datagram, EnvelopeView& out_envelope, std::string& out_error) {
    out_envelope = {};

    ByteReader reader(datagram);
    Byte wire_version = 0;
    if (!reader.ReadU8(wire_version)) {
        out_error = "datagram missing wire_version";
        return false;
    }
    if (wire_version != kWireVersionV1) {
        out_error = "unsupported wire_version";
        return false;
    }

    Byte kind_u8 = 0;
    if (!reader.ReadU8(kind_u8)) {
        out_error = "datagram missing kind";
        return false;
    }

    const MessageKind kind = static_cast<MessageKind>(kind_u8);
    switch (kind) {
        case MessageKind::Control:
        case MessageKind::Command:
        case MessageKind::ChunkSnapshot:
        case MessageKind::ChunkSnapshotBatch:
            break;
        default:
            out_error = "unknown kind";
            return false;
    }

    std::uint64_t payload_len = 0;
    if (!reader.ReadVarUInt(payload_len)) {
        out_error = "datagram missing payload_len";
        return false;
    }
    if (payload_len != reader.Remaining()) {
        out_error = "payload_len does not match datagram size";
        return false;
    }

    const std::size_t payload_len_size_t = static_cast<std::size_t>(payload_len);
    const ByteSpan payload = datagram.subspan(reader.Offset(), payload_len_size_t);

    out_envelope.kind = kind;
    out_envelope.payload = payload;
    out_error.clear();
    return true;
}

void EncodeEnvelopeV1(MessageKind kind, ByteSpan payload, ByteBuffer& out_datagram) {
    ByteWriter writer;
    writer.WriteU8(kWireVersionV1);
    writer.WriteU8(static_cast<Byte>(kind));
    writer.WriteVarUInt(payload.size());
    writer.WriteRawBytes(payload);

    out_datagram = writer.TakeBuffer();
}

}  // namespace novaria::wire
