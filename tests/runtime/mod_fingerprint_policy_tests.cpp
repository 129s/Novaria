#include "runtime/mod_fingerprint_policy.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool passed = true;

    const auto empty_ok = novaria::runtime::EvaluateModFingerprint("", "", false);
    passed &= Expect(
        empty_ok.decision == novaria::runtime::ModFingerprintDecision::Accept,
        "Empty fingerprints should be accepted.");

    const auto equal_ok =
        novaria::runtime::EvaluateModFingerprint("fp1", "fp1", true);
    passed &= Expect(
        equal_ok.decision == novaria::runtime::ModFingerprintDecision::Accept,
        "Equal fingerprints should be accepted.");

    const auto warn_check =
        novaria::runtime::EvaluateModFingerprint("save_fp", "runtime_fp", false);
    passed &= Expect(
        warn_check.decision == novaria::runtime::ModFingerprintDecision::Warn,
        "Mismatch should warn when strict mode is off.");
    passed &= Expect(
        warn_check.message.find("save_fp") != std::string::npos &&
            warn_check.message.find("runtime_fp") != std::string::npos,
        "Warn message should include both fingerprints.");

    const auto reject_check =
        novaria::runtime::EvaluateModFingerprint("save_fp", "runtime_fp", true);
    passed &= Expect(
        reject_check.decision == novaria::runtime::ModFingerprintDecision::Reject,
        "Mismatch should reject when strict mode is on.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_mod_fingerprint_policy_tests\n";
    return 0;
}
