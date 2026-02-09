#include "runtime/mod_script_loader.h"

#include "content/pak.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace novaria::runtime {
namespace {

bool ReadTextFile(
    const std::filesystem::path& file_path,
    std::string& out_text,
    std::string& out_error) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        out_error = "cannot open file: " + file_path.string();
        return false;
    }

    out_text.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    out_error.clear();
    return true;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() ||
        path.has_root_name() || path.has_root_directory()) {
        return false;
    }

    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }

    return true;
}

bool IsPathInsideRoot(
    const std::filesystem::path& root_path,
    const std::filesystem::path& candidate_path,
    std::string& out_error) {
    std::error_code ec;
    const std::filesystem::path canonical_root =
        std::filesystem::weakly_canonical(root_path, ec);
    if (ec) {
        out_error =
            "cannot canonicalize mod root path: " + root_path.string() +
            " (" + ec.message() + ")";
        return false;
    }

    const std::filesystem::path canonical_candidate =
        std::filesystem::weakly_canonical(candidate_path, ec);
    if (ec) {
        out_error =
            "cannot canonicalize script path: " + candidate_path.string() +
            " (" + ec.message() + ")";
        return false;
    }

    const std::filesystem::path relative_path =
        std::filesystem::relative(canonical_candidate, canonical_root, ec);
    if (ec) {
        out_error =
            "cannot resolve script path relative to mod root: " +
            canonical_candidate.string();
        return false;
    }

    if (relative_path.empty() || relative_path.is_absolute()) {
        out_error = "script path escapes mod root";
        return false;
    }

    for (const auto& part : relative_path) {
        if (part == "..") {
            out_error = "script path escapes mod root";
            return false;
        }
    }

    out_error.clear();
    return true;
}

}  // namespace

bool BuildModScriptModules(
    const std::vector<mod::ModManifest>& manifests,
    std::vector<script::ScriptModuleSource>& out_modules,
    std::string& out_error) {
    out_modules.clear();
    for (const auto& manifest : manifests) {
        if (manifest.script_entry.empty()) {
            continue;
        }

        const std::filesystem::path script_entry_path =
            std::filesystem::path(manifest.script_entry).lexically_normal();
        if (!IsSafeRelativePath(script_entry_path)) {
            out_error =
                "Invalid script entry path in mod '" + manifest.name +
                "': " + manifest.script_entry;
            return false;
        }

        std::string module_source;
        if (manifest.container_kind == mod::ModContainerKind::Directory) {
            const std::filesystem::path module_file_path =
                manifest.container_path / script_entry_path;
            std::string path_validation_error;
            if (!IsPathInsideRoot(
                    manifest.container_path,
                    module_file_path,
                    path_validation_error)) {
                out_error =
                    "Invalid script entry path in mod '" + manifest.name +
                    "': " + manifest.script_entry + " (" +
                    path_validation_error + ")";
                return false;
            }

            if (!ReadTextFile(module_file_path, module_source, out_error)) {
                out_error =
                    "Failed to load script entry for mod '" + manifest.name +
                    "': " + out_error;
                return false;
            }
        } else if (manifest.container_kind == mod::ModContainerKind::Pak) {
            content::PakReader pak;
            if (!pak.Open(manifest.container_path, out_error)) {
                out_error =
                    "Failed to open mod pak for mod '" + manifest.name +
                    "': " + out_error;
                return false;
            }

            if (!pak.ReadTextFile(script_entry_path.generic_string(), module_source, out_error)) {
                out_error =
                    "Failed to load script entry for mod '" + manifest.name +
                    "': " + out_error;
                return false;
            }
        } else {
            out_error = "Unsupported mod container kind for mod '" + manifest.name + "'.";
            return false;
        }

        out_modules.push_back(script::ScriptModuleSource{
            .module_name = manifest.name,
            .api_version =
                manifest.script_api_version.empty()
                    ? std::string(script::kScriptApiVersion)
                    : manifest.script_api_version,
            .capabilities =
                manifest.script_capabilities.empty()
                    ? std::vector<std::string>{"event.receive", "tick.receive"}
                    : manifest.script_capabilities,
            .source_code = std::move(module_source),
        });
    }

    out_error.clear();
    return true;
}

}  // namespace novaria::runtime
