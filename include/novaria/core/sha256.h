#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace novaria::core {

class Sha256 final {
public:
    Sha256();

    void Update(std::string_view data);
    void Finalize(std::uint8_t (&out_digest)[32]);
    static std::string HexDigest(std::string_view data);

private:
    void ProcessBlock(const std::uint8_t* block);

    std::uint64_t total_bytes_ = 0;
    std::size_t buffer_size_ = 0;
    std::uint8_t buffer_[64]{};
    std::uint32_t state_[8]{};
};

}  // namespace novaria::core
