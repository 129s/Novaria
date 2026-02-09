#include "core/base64.h"

#include <array>

namespace novaria::core {
namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::array<std::uint8_t, 256> BuildDecodeTable() {
    std::array<std::uint8_t, 256> table{};
    table.fill(0xFF);
    for (std::uint8_t i = 0; i < 64; ++i) {
        table[static_cast<std::uint8_t>(kAlphabet[i])] = i;
    }
    return table;
}

const std::array<std::uint8_t, 256>& DecodeTable() {
    static const std::array<std::uint8_t, 256> table = BuildDecodeTable();
    return table;
}

}  // namespace

std::string Base64Encode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return {};
    }

    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= bytes.size()) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[i]) << 16U) |
            (static_cast<std::uint32_t>(bytes[i + 1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[i + 2]));
        out.push_back(kAlphabet[(chunk >> 18U) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12U) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6U) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
        i += 3;
    }

    const std::size_t remaining = bytes.size() - i;
    if (remaining == 1) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(bytes[i]) << 16U;
        out.push_back(kAlphabet[(chunk >> 18U) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12U) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[i]) << 16U) |
            (static_cast<std::uint32_t>(bytes[i + 1]) << 8U);
        out.push_back(kAlphabet[(chunk >> 18U) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12U) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6U) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

bool TryBase64Decode(
    std::string_view text,
    std::vector<std::uint8_t>& out_bytes,
    std::string& out_error) {
    out_bytes.clear();

    if (text.empty()) {
        out_error.clear();
        return true;
    }

    if ((text.size() % 4) != 0) {
        out_error = "base64 length must be multiple of 4";
        return false;
    }

    const auto& table = DecodeTable();
    std::size_t padding = 0;
    if (!text.empty() && text.back() == '=') {
        padding = 1;
        if (text.size() >= 2 && text[text.size() - 2] == '=') {
            padding = 2;
        }
    }
    if (padding != 0) {
        for (std::size_t i = text.size() - padding; i < text.size(); ++i) {
            if (text[i] != '=') {
                out_error = "invalid base64 padding";
                return false;
            }
        }
    }

    out_bytes.reserve((text.size() / 4) * 3 - padding);

    for (std::size_t i = 0; i < text.size(); i += 4) {
        const char c0 = text[i];
        const char c1 = text[i + 1];
        const char c2 = text[i + 2];
        const char c3 = text[i + 3];

        const std::uint8_t v0 = table[static_cast<std::uint8_t>(c0)];
        const std::uint8_t v1 = table[static_cast<std::uint8_t>(c1)];
        const std::uint8_t v2 = (c2 == '=') ? 0 : table[static_cast<std::uint8_t>(c2)];
        const std::uint8_t v3 = (c3 == '=') ? 0 : table[static_cast<std::uint8_t>(c3)];

        if (v0 == 0xFF || v1 == 0xFF || v2 == 0xFF || v3 == 0xFF) {
            out_error = "invalid base64 character";
            return false;
        }

        const bool is_last = (i + 4 == text.size());
        if (!is_last && (c2 == '=' || c3 == '=')) {
            out_error = "base64 padding only allowed at end";
            return false;
        }
        if (c2 == '=' && c3 != '=') {
            out_error = "invalid base64 padding";
            return false;
        }

        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(v0) << 18U) |
            (static_cast<std::uint32_t>(v1) << 12U) |
            (static_cast<std::uint32_t>(v2) << 6U) |
            static_cast<std::uint32_t>(v3);

        out_bytes.push_back(static_cast<std::uint8_t>((chunk >> 16U) & 0xFF));
        if (c2 != '=') {
            out_bytes.push_back(static_cast<std::uint8_t>((chunk >> 8U) & 0xFF));
        }
        if (c3 != '=') {
            out_bytes.push_back(static_cast<std::uint8_t>(chunk & 0xFF));
        }
    }

    out_error.clear();
    return true;
}

}  // namespace novaria::core

