#include "runtime/runtime_paths.h"

#include <system_error>

namespace novaria::runtime {
namespace {

std::filesystem::path ResolvePathRelativeToRoot(
    const std::filesystem::path& value,
    const std::filesystem::path& exe_dir) {
    if (value.empty()) {
        return value;
    }

    if (value.is_absolute()) {
        return value.lexically_normal();
    }

    return (exe_dir / value).lexically_normal();
}

}  // namespace

RuntimePaths ResolveRuntimePaths(
    const std::filesystem::path& exe_dir,
    const core::GameConfig& config) {
    const std::filesystem::path default_mod_root = (exe_dir / "mods").lexically_normal();
    const std::filesystem::path default_save_root = (exe_dir / "saves").lexically_normal();

    return RuntimePaths{
        .mod_root = config.mod_root.empty()
            ? default_mod_root
            : ResolvePathRelativeToRoot(std::filesystem::path(config.mod_root), exe_dir),
        .save_root = config.save_root.empty()
            ? default_save_root
            : ResolvePathRelativeToRoot(std::filesystem::path(config.save_root), exe_dir),
    };
}

}  // namespace novaria::runtime
