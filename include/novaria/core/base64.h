#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::core {

std::string Base64Encode(std::span<const std::uint8_t> bytes);

bool TryBase64Decode(
    std::string_view text,
    std::vector<std::uint8_t>& out_bytes,
    std::string& out_error);

}  // namespace novaria::core

