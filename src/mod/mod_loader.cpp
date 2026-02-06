#include "mod/mod_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace novaria::mod {

bool ModLoader::Initialize(const std::filesystem::path& mod_root, std::string& out_error) {
    if (!std::filesystem::exists(mod_root)) {
        out_error = "Mod root does not exist: " + mod_root.string();
        return false;
    }

    if (!std::filesystem::is_directory(mod_root)) {
        out_error = "Mod root is not a directory: " + mod_root.string();
        return false;
    }

    mod_root_ = mod_root;
    initialized_ = true;
    out_error.clear();
    return true;
}

void ModLoader::Shutdown() {
    initialized_ = false;
    mod_root_.clear();
}

bool ModLoader::LoadAll(std::vector<ModManifest>& out_manifests, std::string& out_error) const {
    if (!initialized_) {
        out_error = "Mod loader is not initialized.";
        return false;
    }

    out_manifests.clear();
    std::vector<ModManifest> loaded_manifests;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(mod_root_, ec)) {
        if (ec) {
            out_error = "Failed to iterate mod directory: " + ec.message();
            return false;
        }

        if (!entry.is_directory()) {
            continue;
        }

        const std::filesystem::path manifest_path = entry.path() / "mod.toml";
        if (!std::filesystem::exists(manifest_path)) {
            continue;
        }

        ModManifest manifest{};
        if (!ParseManifestFile(manifest_path, manifest, out_error)) {
            out_error = "Invalid mod manifest '" + manifest_path.string() + "': " + out_error;
            return false;
        }

        manifest.root_path = entry.path();
        loaded_manifests.push_back(std::move(manifest));
    }

    return BuildDependencyOrderedManifestList(loaded_manifests, out_manifests, out_error);
}

std::string ModLoader::BuildManifestFingerprint(const std::vector<ModManifest>& manifests) {
    std::vector<std::string> canonical_entries;
    canonical_entries.reserve(manifests.size());
    for (const auto& manifest : manifests) {
        std::vector<std::string> normalized_dependencies = manifest.dependencies;
        std::sort(normalized_dependencies.begin(), normalized_dependencies.end());

        std::string canonical_entry =
            manifest.name + "|" + manifest.version + "|" + manifest.description + "|deps=";
        for (std::size_t dependency_index = 0;
             dependency_index < normalized_dependencies.size();
             ++dependency_index) {
            if (dependency_index > 0) {
                canonical_entry += ",";
            }
            canonical_entry += normalized_dependencies[dependency_index];
        }

        canonical_entries.push_back(std::move(canonical_entry));
    }
    std::sort(canonical_entries.begin(), canonical_entries.end());

    std::uint64_t hash = 1469598103934665603ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    for (const auto& entry : canonical_entries) {
        for (const unsigned char ch : entry) {
            hash ^= ch;
            hash *= kFnvPrime;
        }
        hash ^= static_cast<unsigned char>('\n');
        hash *= kFnvPrime;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::string ModLoader::Trim(const std::string& value) {
    std::string result = value;
    auto is_not_space = [](unsigned char ch) { return !std::isspace(ch); };
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), is_not_space));
    result.erase(std::find_if(result.rbegin(), result.rend(), is_not_space).base(), result.end());
    return result;
}

bool ModLoader::ParseQuotedString(const std::string& value, std::string& out_text) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return false;
    }

    out_text = value.substr(1, value.size() - 2);
    return true;
}

bool ModLoader::ParseQuotedStringArray(
    const std::string& value,
    std::vector<std::string>& out_items) {
    out_items.clear();
    const std::string trimmed_value = Trim(value);
    if (trimmed_value.size() < 2 || trimmed_value.front() != '[' || trimmed_value.back() != ']') {
        return false;
    }

    const std::string inner = Trim(trimmed_value.substr(1, trimmed_value.size() - 2));
    if (inner.empty()) {
        return true;
    }

    std::size_t cursor = 0;
    while (cursor < inner.size()) {
        if (inner[cursor] != '"') {
            return false;
        }

        const std::size_t closing_quote = inner.find('"', cursor + 1);
        if (closing_quote == std::string::npos) {
            return false;
        }

        out_items.push_back(inner.substr(cursor + 1, closing_quote - cursor - 1));
        cursor = closing_quote + 1;
        while (cursor < inner.size() && std::isspace(static_cast<unsigned char>(inner[cursor]))) {
            ++cursor;
        }

        if (cursor >= inner.size()) {
            break;
        }

        if (inner[cursor] != ',') {
            return false;
        }

        ++cursor;
        while (cursor < inner.size() && std::isspace(static_cast<unsigned char>(inner[cursor]))) {
            ++cursor;
        }
    }

    return true;
}

bool ModLoader::ParseManifestFile(
    const std::filesystem::path& manifest_path,
    ModManifest& out_manifest,
    std::string& out_error) {
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        out_error = "cannot open file";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        const std::string key = Trim(line.substr(0, eq_pos));
        const std::string value = Trim(line.substr(eq_pos + 1));

        if (key == "name") {
            if (!ParseQuotedString(value, out_manifest.name)) {
                out_error = "name must be quoted string";
                return false;
            }
            continue;
        }

        if (key == "version") {
            if (!ParseQuotedString(value, out_manifest.version)) {
                out_error = "version must be quoted string";
                return false;
            }
            continue;
        }

        if (key == "description") {
            if (!ParseQuotedString(value, out_manifest.description)) {
                out_error = "description must be quoted string";
                return false;
            }
            continue;
        }

        if (key == "dependencies") {
            if (!ParseQuotedStringArray(value, out_manifest.dependencies)) {
                out_error = "dependencies must be quoted string array";
                return false;
            }
            continue;
        }
    }

    if (out_manifest.name.empty()) {
        out_error = "missing required field 'name'";
        return false;
    }

    if (out_manifest.version.empty()) {
        out_error = "missing required field 'version'";
        return false;
    }

    return true;
}

bool ModLoader::BuildDependencyOrderedManifestList(
    const std::vector<ModManifest>& loaded_manifests,
    std::vector<ModManifest>& out_ordered_manifests,
    std::string& out_error) {
    out_ordered_manifests.clear();
    if (loaded_manifests.empty()) {
        out_error.clear();
        return true;
    }

    std::vector<ModManifest> manifests = loaded_manifests;
    std::sort(
        manifests.begin(),
        manifests.end(),
        [](const ModManifest& lhs, const ModManifest& rhs) { return lhs.name < rhs.name; });

    std::unordered_map<std::string, std::size_t> name_to_index;
    name_to_index.reserve(manifests.size());
    for (std::size_t index = 0; index < manifests.size(); ++index) {
        const auto [insert_it, inserted] = name_to_index.emplace(manifests[index].name, index);
        if (!inserted) {
            out_error = "Duplicate mod name detected: " + manifests[index].name;
            return false;
        }
    }

    enum class VisitState : std::uint8_t {
        Unvisited = 0,
        Visiting = 1,
        Visited = 2,
    };
    std::vector<VisitState> visit_states(manifests.size(), VisitState::Unvisited);
    std::vector<std::size_t> ordered_indices;
    ordered_indices.reserve(manifests.size());

    std::function<bool(std::size_t)> visit_manifest = [&](std::size_t manifest_index) {
        VisitState& state = visit_states[manifest_index];
        if (state == VisitState::Visited) {
            return true;
        }
        if (state == VisitState::Visiting) {
            out_error = "Cyclic mod dependency detected at: " + manifests[manifest_index].name;
            return false;
        }

        state = VisitState::Visiting;
        const ModManifest& manifest = manifests[manifest_index];
        for (const auto& dependency_name : manifest.dependencies) {
            const auto dependency_it = name_to_index.find(dependency_name);
            if (dependency_it == name_to_index.end()) {
                out_error =
                    "Missing dependency '" + dependency_name + "' required by mod '" +
                    manifest.name + "'";
                return false;
            }

            if (!visit_manifest(dependency_it->second)) {
                return false;
            }
        }

        state = VisitState::Visited;
        ordered_indices.push_back(manifest_index);
        return true;
    };

    for (std::size_t manifest_index = 0; manifest_index < manifests.size(); ++manifest_index) {
        if (!visit_manifest(manifest_index)) {
            return false;
        }
    }

    out_ordered_manifests.reserve(ordered_indices.size());
    for (std::size_t manifest_index : ordered_indices) {
        out_ordered_manifests.push_back(manifests[manifest_index]);
    }

    out_error.clear();
    return true;
}

}  // namespace novaria::mod
