#include "mod/mod_fingerprint.h"

#include "content/pak.h"
#include "core/sha256.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace novaria::mod {
namespace {

void AppendLengthPrefixedToken(std::string& out_payload, std::string_view token) {
    out_payload += std::to_string(token.size());
    out_payload.push_back(':');
    out_payload.append(token.data(), token.size());
    out_payload.push_back('|');
}

void AppendLengthPrefixedField(
    std::string& out_payload,
    std::string_view field_name,
    std::string_view field_value) {
    AppendLengthPrefixedToken(out_payload, field_name);
    AppendLengthPrefixedToken(out_payload, field_value);
}

void AppendLengthPrefixedStringListField(
    std::string& out_payload,
    std::string_view field_name,
    const std::vector<std::string>& values) {
    AppendLengthPrefixedToken(out_payload, field_name);
    out_payload += std::to_string(values.size());
    out_payload.push_back('#');
    for (const std::string& value : values) {
        AppendLengthPrefixedToken(out_payload, value);
    }
}

std::string StripUtf8Bom(std::string text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return text.substr(3);
    }
    return text;
}

std::string NormalizeTextNewlines(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            normalized.push_back('\n');
            continue;
        }
        normalized.push_back(ch);
    }
    return normalized;
}

bool ReadTextFileNormalized(
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
    out_text = StripUtf8Bom(std::move(out_text));
    out_text = NormalizeTextNewlines(std::move(out_text));
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

bool ComputeScriptDigestHex(
    const ModManifest& manifest,
    std::string& out_hex_digest,
    std::string& out_error) {
    out_hex_digest.clear();
    if (manifest.script_entry.empty()) {
        out_error.clear();
        return true;
    }

    const std::filesystem::path script_entry_path =
        std::filesystem::path(manifest.script_entry).lexically_normal();
    if (!IsSafeRelativePath(script_entry_path)) {
        out_error = "Invalid script entry path in mod '" + manifest.name + "': " +
            manifest.script_entry;
        return false;
    }

    std::string script_text;
    if (manifest.container_kind == ModContainerKind::Directory) {
        const std::filesystem::path module_file_path =
            manifest.container_path / script_entry_path;
        std::string path_validation_error;
        if (!IsPathInsideRoot(manifest.container_path, module_file_path, path_validation_error)) {
            out_error = "Invalid script entry path in mod '" + manifest.name + "': " +
                manifest.script_entry + " (" + path_validation_error + ")";
            return false;
        }

        if (!ReadTextFileNormalized(module_file_path, script_text, out_error)) {
            out_error = "Failed to load script entry for mod '" + manifest.name + "': " + out_error;
            return false;
        }
    } else if (manifest.container_kind == ModContainerKind::Pak) {
        content::PakReader pak;
        if (!pak.Open(manifest.container_path, out_error)) {
            out_error = "Failed to open mod pak for mod '" + manifest.name + "': " + out_error;
            return false;
        }

        if (!pak.ReadTextFile(script_entry_path.generic_string(), script_text, out_error)) {
            out_error = "Failed to load script entry for mod '" + manifest.name + "': " + out_error;
            return false;
        }
    } else {
        out_error = "Unsupported mod container kind for mod '" + manifest.name + "'.";
        return false;
    }

    out_hex_digest = core::Sha256::HexDigest(script_text);
    out_error.clear();
    return true;
}

}  // namespace

bool BuildGameplayFingerprint(
    const std::vector<ModManifest>& manifests,
    std::string& out_fingerprint,
    std::string& out_error) {
    out_fingerprint.clear();
    if (manifests.empty()) {
        out_error.clear();
        return true;
    }

    std::vector<std::string> canonical_entries;
    canonical_entries.reserve(manifests.size());
    for (const auto& manifest : manifests) {
        std::vector<std::string> normalized_dependencies = manifest.dependencies;
        std::sort(normalized_dependencies.begin(), normalized_dependencies.end());

        std::vector<std::string> normalized_capabilities = manifest.script_capabilities;
        std::sort(normalized_capabilities.begin(), normalized_capabilities.end());

        std::vector<std::string> normalized_items;
        normalized_items.reserve(manifest.items.size());
        for (const auto& item : manifest.items) {
            normalized_items.push_back(item.id + ":" + item.behavior);
        }
        std::sort(normalized_items.begin(), normalized_items.end());

        std::vector<std::string> normalized_recipes;
        normalized_recipes.reserve(manifest.recipes.size());
        for (const auto& recipe : manifest.recipes) {
            normalized_recipes.push_back(
                recipe.id + ":" + recipe.input_item_id + "*" + std::to_string(recipe.input_amount) +
                "->" + recipe.output_item_id + "*" + std::to_string(recipe.output_amount));
        }
        std::sort(normalized_recipes.begin(), normalized_recipes.end());

        std::vector<std::string> normalized_npcs;
        normalized_npcs.reserve(manifest.npcs.size());
        for (const auto& npc : manifest.npcs) {
            normalized_npcs.push_back(
                npc.id + ":" + std::to_string(npc.max_health) + ":" + npc.behavior);
        }
        std::sort(normalized_npcs.begin(), normalized_npcs.end());

        std::string script_sha256_hex;
        if (!ComputeScriptDigestHex(manifest, script_sha256_hex, out_error)) {
            return false;
        }

        std::string canonical_entry;
        canonical_entry.reserve(320);
        AppendLengthPrefixedField(canonical_entry, "name", manifest.name);
        AppendLengthPrefixedField(canonical_entry, "version", manifest.version);
        AppendLengthPrefixedField(canonical_entry, "description", manifest.description);
        AppendLengthPrefixedField(canonical_entry, "script_entry", manifest.script_entry);
        AppendLengthPrefixedField(canonical_entry, "script_sha256", script_sha256_hex);
        AppendLengthPrefixedField(
            canonical_entry,
            "script_api_version",
            manifest.script_api_version);
        AppendLengthPrefixedStringListField(
            canonical_entry,
            "script_capabilities",
            normalized_capabilities);
        AppendLengthPrefixedStringListField(
            canonical_entry,
            "dependencies",
            normalized_dependencies);
        AppendLengthPrefixedStringListField(canonical_entry, "items", normalized_items);
        AppendLengthPrefixedStringListField(canonical_entry, "recipes", normalized_recipes);
        AppendLengthPrefixedStringListField(canonical_entry, "npcs", normalized_npcs);

        canonical_entries.push_back(std::move(canonical_entry));
    }
    std::sort(canonical_entries.begin(), canonical_entries.end());

    std::string canonical_payload;
    canonical_payload.reserve(canonical_entries.size() * 160);
    for (const auto& entry : canonical_entries) {
        canonical_payload += entry;
        canonical_payload.push_back('\n');
    }

    out_fingerprint = core::Sha256::HexDigest(canonical_payload);
    out_error.clear();
    return true;
}

}  // namespace novaria::mod

