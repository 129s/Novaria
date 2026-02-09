#pragma once

#include "core/config.h"

#include <filesystem>

namespace novaria::runtime {

struct RuntimePaths final {
    std::filesystem::path mod_root;
    std::filesystem::path save_root;
};

RuntimePaths ResolveRuntimePaths(
    const std::filesystem::path& exe_dir,
    const core::GameConfig& config);

}  // namespace novaria::runtime
