#include "runtime/mod_pipeline.h"

#include "core/logger.h"
#include "runtime/mod_script_loader.h"

#include <algorithm>

namespace novaria::runtime {

bool LoadModsAndScripts(
    const std::filesystem::path& mod_root,
    mod::ModLoader& mod_loader,
    std::vector<mod::ModManifest>& out_manifests,
    std::string& out_fingerprint,
    std::vector<script::ScriptModuleSource>& out_modules,
    std::string& out_error) {
    out_manifests.clear();
    out_fingerprint.clear();
    out_modules.clear();

    std::string mod_error;
    if (!mod_loader.Initialize(mod_root, mod_error)) {
        out_error = "Mod loader initialize failed: " + mod_error;
        return false;
    }

    if (!mod_loader.LoadAll(out_manifests, mod_error)) {
        out_error = "Mod loading failed: " + mod_error;
        out_manifests.clear();
        out_fingerprint.clear();
        return false;
    }

    const auto core_iter =
        std::find_if(
            out_manifests.begin(),
            out_manifests.end(),
            [](const mod::ModManifest& manifest) { return manifest.name == "core"; });
    if (core_iter == out_manifests.end()) {
        out_error = "Required mod missing: core";
        out_manifests.clear();
        out_fingerprint.clear();
        return false;
    }
    if (core_iter->script_entry.empty()) {
        out_error = "Required mod has no script_entry: core";
        out_manifests.clear();
        out_fingerprint.clear();
        return false;
    }

    out_fingerprint = mod::ModLoader::BuildManifestFingerprint(out_manifests);
    std::size_t item_definition_count = 0;
    std::size_t recipe_definition_count = 0;
    std::size_t npc_definition_count = 0;
    for (const auto& manifest : out_manifests) {
        item_definition_count += manifest.items.size();
        recipe_definition_count += manifest.recipes.size();
        npc_definition_count += manifest.npcs.size();
    }
    core::Logger::Info("mod", "Loaded mods: " + std::to_string(out_manifests.size()));
    core::Logger::Info(
        "mod",
        "Loaded mod content definitions: items=" + std::to_string(item_definition_count) +
            ", recipes=" + std::to_string(recipe_definition_count) +
            ", npcs=" + std::to_string(npc_definition_count));
    core::Logger::Info("mod", "Manifest fingerprint: " + out_fingerprint);

    if (!BuildModScriptModules(out_manifests, out_modules, out_error)) {
        out_manifests.clear();
        out_fingerprint.clear();
        return false;
    }
    if (out_modules.empty()) {
        out_error = "No script modules loaded (core script required).";
        out_manifests.clear();
        out_fingerprint.clear();
        out_modules.clear();
        return false;
    }

    out_error.clear();
    return true;
}

}  // namespace novaria::runtime
