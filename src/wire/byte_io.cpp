#include "wire/byte_io.h"

#include <limits>

namespace novaria::wire {
namespace {

constexpr std::uint64_t ZigZagEncode(std::int64_t value) {
    return (static_cast<std::uint64_t>(value) << 1U) ^
        static_cast<std::uint64_t>(value >> 63);
}

constexpr std::int64_t ZigZagDecode(std::uint64_t value) {
    return static_cast<std::int64_t>((value >> 1U) ^ (~(value & 1U) + 1U));
}

}  // namespace

void ByteWriter::Clear() {
    buffer_.clear();
}

const ByteBuffer& ByteWriter::Buffer() const {
    return buffer_;
}

ByteBuffer&& ByteWriter::TakeBuffer() {
    return std::move(buffer_);
}

void ByteWriter::WriteU8(Byte value) {
    buffer_.push_back(value);
}

void ByteWriter::WriteVarUInt(std::uint64_t value) {
    while (value >= 0x80) {
        buffer_.push_back(static_cast<Byte>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buffer_.push_back(static_cast<Byte>(value));
}

void ByteWriter::WriteVarInt(std::int64_t value) {
    WriteVarUInt(ZigZagEncode(value));
}

void ByteWriter::WriteRawBytes(ByteSpan bytes) {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void ByteWriter::WriteBytes(ByteSpan bytes) {
    WriteVarUInt(bytes.size());
    WriteRawBytes(bytes);
}

void ByteWriter::WriteString(std::string_view text) {
    WriteBytes(ByteSpan(reinterpret_cast<const Byte*>(text.data()), text.size()));
}

ByteReader::ByteReader(ByteSpan bytes) : bytes_(bytes) {}

std::size_t ByteReader::Offset() const {
    return offset_;
}

std::size_t ByteReader::Remaining() const {
    if (offset_ >= bytes_.size()) {
        return 0;
    }
    return bytes_.size() - offset_;
}

bool ByteReader::IsFullyConsumed() const {
    return offset_ == bytes_.size();
}

bool ByteReader::ReadU8(Byte& out_value) {
    if (offset_ >= bytes_.size()) {
        return false;
    }

    out_value = bytes_[offset_++];
    return true;
}

bool ByteReader::ReadVarUInt(std::uint64_t& out_value) {
    out_value = 0;

    std::uint32_t shift = 0;
    for (int i = 0; i < 10; ++i) {
        Byte byte = 0;
        if (!ReadU8(byte)) {
            return false;
        }

        const std::uint64_t chunk = static_cast<std::uint64_t>(byte & 0x7F);
        if (shift >= 64 || (chunk << shift) >> shift != chunk) {
            return false;
        }

        out_value |= (chunk << shift);
        if ((byte & 0x80) == 0) {
            return true;
        }

        shift += 7;
    }

    return false;
}

bool ByteReader::ReadVarInt(std::int64_t& out_value) {
    std::uint64_t encoded = 0;
    if (!ReadVarUInt(encoded)) {
        return false;
    }

    out_value = ZigZagDecode(encoded);
    return true;
}

bool ByteReader::ReadRawBytes(std::size_t length, ByteSpan& out_bytes) {
    if (length > Remaining()) {
        return false;
    }

    out_bytes = bytes_.subspan(offset_, length);
    offset_ += length;
    return true;
}

bool ByteReader::ReadBytes(ByteSpan& out_bytes) {
    std::uint64_t length = 0;
    if (!ReadVarUInt(length)) {
        return false;
    }
    if (length > Remaining()) {
        return false;
    }

    const std::size_t length_size_t = static_cast<std::size_t>(length);
    return ReadRawBytes(length_size_t, out_bytes);
}

bool ByteReader::ReadString(std::string& out_text) {
    ByteSpan bytes;
    if (!ReadBytes(bytes)) {
        return false;
    }

    out_text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

}  // namespace novaria::wire
