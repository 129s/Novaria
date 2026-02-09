#pragma once

#include "mod/mod_loader.h"

#include <string>
#include <vector>

namespace novaria::mod {

bool BuildGameplayFingerprint(
    const std::vector<ModManifest>& manifests,
    std::string& out_fingerprint,
    std::string& out_error);

}  // namespace novaria::mod

