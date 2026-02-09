#include "runtime/mod_fingerprint_policy.h"

namespace novaria::runtime {

ModFingerprintCheck EvaluateModFingerprint(
    std::string_view save_fingerprint,
    std::string_view runtime_fingerprint,
    bool strict) {
    ModFingerprintCheck result{};
    if (save_fingerprint.empty() ||
        runtime_fingerprint.empty() ||
        save_fingerprint == runtime_fingerprint) {
        return result;
    }

    result.decision =
        strict ? ModFingerprintDecision::Reject : ModFingerprintDecision::Warn;
    result.message =
        "Gameplay fingerprint mismatch: save=" +
        std::string(save_fingerprint) +
        ", runtime=" +
        std::string(runtime_fingerprint);
    return result;
}

}  // namespace novaria::runtime
