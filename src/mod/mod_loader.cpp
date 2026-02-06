#include "mod/mod_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
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
        out_manifests.push_back(std::move(manifest));
    }

    out_error.clear();
    return true;
}

std::string ModLoader::BuildManifestFingerprint(const std::vector<ModManifest>& manifests) {
    std::vector<std::string> canonical_entries;
    canonical_entries.reserve(manifests.size());
    for (const auto& manifest : manifests) {
        canonical_entries.push_back(
            manifest.name + "|" + manifest.version + "|" + manifest.description);
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

}  // namespace novaria::mod
