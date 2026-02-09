#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace novaria::runtime {

enum class ModFingerprintDecision : std::uint8_t {
    Accept = 0,
    Warn = 1,
    Reject = 2,
};

struct ModFingerprintCheck final {
    ModFingerprintDecision decision = ModFingerprintDecision::Accept;
    std::string message;
};

ModFingerprintCheck EvaluateModFingerprint(
    std::string_view save_fingerprint,
    std::string_view runtime_fingerprint,
    bool strict);

}  // namespace novaria::runtime
