#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::wire {

using Byte = std::uint8_t;
using ByteBuffer = std::vector<Byte>;
using ByteSpan = std::span<const Byte>;

class ByteWriter final {
public:
    void Clear();
    const ByteBuffer& Buffer() const;
    ByteBuffer&& TakeBuffer();

    void WriteU8(Byte value);
    void WriteVarUInt(std::uint64_t value);
    void WriteVarInt(std::int64_t value);
    void WriteRawBytes(ByteSpan bytes);
    void WriteBytes(ByteSpan bytes);
    void WriteString(std::string_view text);

private:
    ByteBuffer buffer_;
};

class ByteReader final {
public:
    explicit ByteReader(ByteSpan bytes);

    std::size_t Offset() const;
    std::size_t Remaining() const;
    bool IsFullyConsumed() const;

    bool ReadU8(Byte& out_value);
    bool ReadVarUInt(std::uint64_t& out_value);
    bool ReadVarInt(std::int64_t& out_value);
    bool ReadRawBytes(std::size_t length, ByteSpan& out_bytes);
    bool ReadBytes(ByteSpan& out_bytes);
    bool ReadString(std::string& out_text);

private:
    ByteSpan bytes_;
    std::size_t offset_ = 0;
};

}  // namespace novaria::wire
