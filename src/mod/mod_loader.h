#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace novaria::mod {

struct ModManifest final {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> dependencies;
    std::filesystem::path root_path;
};

class ModLoader final {
public:
    bool Initialize(const std::filesystem::path& mod_root, std::string& out_error);
    void Shutdown();
    bool LoadAll(std::vector<ModManifest>& out_manifests, std::string& out_error) const;
    static std::string BuildManifestFingerprint(const std::vector<ModManifest>& manifests);

private:
    static std::string Trim(const std::string& value);
    static bool ParseQuotedString(const std::string& value, std::string& out_text);
    static bool ParseQuotedStringArray(
        const std::string& value,
        std::vector<std::string>& out_items);
    static bool ParseManifestFile(
        const std::filesystem::path& manifest_path,
        ModManifest& out_manifest,
        std::string& out_error);
    static bool BuildDependencyOrderedManifestList(
        const std::vector<ModManifest>& loaded_manifests,
        std::vector<ModManifest>& out_ordered_manifests,
        std::string& out_error);

    bool initialized_ = false;
    std::filesystem::path mod_root_;
};

}  // namespace novaria::mod
