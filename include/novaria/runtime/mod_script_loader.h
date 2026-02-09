#pragma once

#include "mod/mod_loader.h"
#include "script/script_host.h"

#include <string>
#include <vector>

namespace novaria::runtime {

bool BuildModScriptModules(
    const std::vector<mod::ModManifest>& manifests,
    std::vector<script::ScriptModuleSource>& out_modules,
    std::string& out_error);

}  // namespace novaria::runtime
