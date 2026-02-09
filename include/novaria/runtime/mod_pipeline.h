#pragma once

#include "mod/mod_loader.h"
#include "script/script_host.h"

#include <filesystem>
#include <string>
#include <vector>

namespace novaria::runtime {

bool LoadModsAndScripts(
    const std::filesystem::path& mod_root,
    mod::ModLoader& mod_loader,
    std::vector<mod::ModManifest>& out_manifests,
    std::string& out_fingerprint,
    std::vector<script::ScriptModuleSource>& out_modules,
    std::string& out_error);

}  // namespace novaria::runtime
