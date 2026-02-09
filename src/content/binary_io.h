#pragma once

#include <cstdint>
#include <istream>
#include <ostream>

namespace novaria::content::detail {

inline void WriteU32Le(std::ostream& out, std::uint32_t value) {
    const unsigned char bytes[4]{
        static_cast<unsigned char>(value & 0xFFU),
        static_cast<unsigned char>((value >> 8) & 0xFFU),
        static_cast<unsigned char>((value >> 16) & 0xFFU),
        static_cast<unsigned char>((value >> 24) & 0xFFU),
    };
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

inline void WriteU64Le(std::ostream& out, std::uint64_t value) {
    const unsigned char bytes[8]{
        static_cast<unsigned char>(value & 0xFFU),
        static_cast<unsigned char>((value >> 8) & 0xFFU),
        static_cast<unsigned char>((value >> 16) & 0xFFU),
        static_cast<unsigned char>((value >> 24) & 0xFFU),
        static_cast<unsigned char>((value >> 32) & 0xFFU),
        static_cast<unsigned char>((value >> 40) & 0xFFU),
        static_cast<unsigned char>((value >> 48) & 0xFFU),
        static_cast<unsigned char>((value >> 56) & 0xFFU),
    };
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

inline bool ReadU32Le(std::istream& in, std::uint32_t& out_value) {
    unsigned char bytes[4]{};
    in.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (!in) {
        return false;
    }
    out_value =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

inline bool ReadU64Le(std::istream& in, std::uint64_t& out_value) {
    unsigned char bytes[8]{};
    in.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (!in) {
        return false;
    }
    out_value =
        static_cast<std::uint64_t>(bytes[0]) |
        (static_cast<std::uint64_t>(bytes[1]) << 8) |
        (static_cast<std::uint64_t>(bytes[2]) << 16) |
        (static_cast<std::uint64_t>(bytes[3]) << 24) |
        (static_cast<std::uint64_t>(bytes[4]) << 32) |
        (static_cast<std::uint64_t>(bytes[5]) << 40) |
        (static_cast<std::uint64_t>(bytes[6]) << 48) |
        (static_cast<std::uint64_t>(bytes[7]) << 56);
    return true;
}

}  // namespace novaria::content::detail

