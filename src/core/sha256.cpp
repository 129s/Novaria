#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace novaria::core {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

constexpr std::array<std::uint32_t, 8> kInitialState = {
    0x6a09e667U,
    0xbb67ae85U,
    0x3c6ef372U,
    0xa54ff53aU,
    0x510e527fU,
    0x9b05688cU,
    0x1f83d9abU,
    0x5be0cd19U,
};

inline std::uint32_t RotateRight(std::uint32_t value, std::uint32_t shift) {
    return (value >> shift) | (value << (32U - shift));
}

inline std::uint32_t Choice(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline std::uint32_t Majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline std::uint32_t BigSigma0(std::uint32_t value) {
    return RotateRight(value, 2U) ^ RotateRight(value, 13U) ^ RotateRight(value, 22U);
}

inline std::uint32_t BigSigma1(std::uint32_t value) {
    return RotateRight(value, 6U) ^ RotateRight(value, 11U) ^ RotateRight(value, 25U);
}

inline std::uint32_t SmallSigma0(std::uint32_t value) {
    return RotateRight(value, 7U) ^ RotateRight(value, 18U) ^ (value >> 3U);
}

inline std::uint32_t SmallSigma1(std::uint32_t value) {
    return RotateRight(value, 17U) ^ RotateRight(value, 19U) ^ (value >> 10U);
}

std::string BytesToHex(const std::uint8_t* bytes, std::size_t byte_count) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex;
    hex.resize(byte_count * 2);
    for (std::size_t index = 0; index < byte_count; ++index) {
        const std::uint8_t byte = bytes[index];
        hex[index * 2] = kHexDigits[byte >> 4];
        hex[index * 2 + 1] = kHexDigits[byte & 0x0F];
    }
    return hex;
}

}  // namespace

Sha256::Sha256()
    : state_{
          kInitialState[0],
          kInitialState[1],
          kInitialState[2],
          kInitialState[3],
          kInitialState[4],
          kInitialState[5],
          kInitialState[6],
          kInitialState[7],
      } {}

void Sha256::Update(std::string_view data) {
    if (data.empty()) {
        return;
    }

    std::size_t cursor = 0;
    while (cursor < data.size()) {
        const std::size_t remaining = data.size() - cursor;
        const std::size_t writable = std::min(remaining, sizeof(buffer_) - buffer_size_);
        std::memcpy(
            buffer_ + buffer_size_,
            data.data() + cursor,
            writable);
        buffer_size_ += writable;
        cursor += writable;
        total_bytes_ += writable;

        if (buffer_size_ == sizeof(buffer_)) {
            ProcessBlock(buffer_);
            buffer_size_ = 0;
        }
    }
}

void Sha256::Finalize(std::uint8_t (&out_digest)[32]) {
    const std::uint64_t bit_length = total_bytes_ * 8ULL;
    buffer_[buffer_size_++] = 0x80U;

    if (buffer_size_ > 56U) {
        while (buffer_size_ < 64U) {
            buffer_[buffer_size_++] = 0U;
        }
        ProcessBlock(buffer_);
        buffer_size_ = 0;
    }

    while (buffer_size_ < 56U) {
        buffer_[buffer_size_++] = 0U;
    }

    for (int index = 7; index >= 0; --index) {
        buffer_[buffer_size_++] = static_cast<std::uint8_t>((bit_length >> (index * 8)) & 0xFFU);
    }

    ProcessBlock(buffer_);
    buffer_size_ = 0;

    for (std::size_t state_index = 0; state_index < 8; ++state_index) {
        const std::uint32_t word = state_[state_index];
        out_digest[state_index * 4] = static_cast<std::uint8_t>((word >> 24) & 0xFFU);
        out_digest[state_index * 4 + 1] = static_cast<std::uint8_t>((word >> 16) & 0xFFU);
        out_digest[state_index * 4 + 2] = static_cast<std::uint8_t>((word >> 8) & 0xFFU);
        out_digest[state_index * 4 + 3] = static_cast<std::uint8_t>(word & 0xFFU);
    }
}

std::string Sha256::HexDigest(std::string_view data) {
    Sha256 hasher;
    hasher.Update(data);
    std::uint8_t digest[32]{};
    hasher.Finalize(digest);
    return BytesToHex(digest, sizeof(digest));
}

void Sha256::ProcessBlock(const std::uint8_t* block) {
    std::uint32_t message_schedule[64]{};
    for (std::size_t index = 0; index < 16; ++index) {
        const std::size_t offset = index * 4;
        message_schedule[index] =
            (static_cast<std::uint32_t>(block[offset]) << 24) |
            (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
            static_cast<std::uint32_t>(block[offset + 3]);
    }

    for (std::size_t index = 16; index < 64; ++index) {
        message_schedule[index] =
            SmallSigma1(message_schedule[index - 2]) +
            message_schedule[index - 7] +
            SmallSigma0(message_schedule[index - 15]) +
            message_schedule[index - 16];
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t index = 0; index < 64; ++index) {
        const std::uint32_t t1 =
            h + BigSigma1(e) + Choice(e, f, g) + kRoundConstants[index] + message_schedule[index];
        const std::uint32_t t2 = BigSigma0(a) + Majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

}  // namespace novaria::core
