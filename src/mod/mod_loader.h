#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace novaria::mod {

struct ModItemDefinition final {
    std::string id;
    std::string behavior;
};

struct ModRecipeDefinition final {
    std::string id;
    std::string input_item_id;
    std::uint32_t input_amount = 0;
    std::string output_item_id;
    std::uint32_t output_amount = 0;
};

struct ModNpcDefinition final {
    std::string id;
    std::uint32_t max_health = 0;
    std::string behavior;
};

struct ModManifest final {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> dependencies;
    std::vector<ModItemDefinition> items;
    std::vector<ModRecipeDefinition> recipes;
    std::vector<ModNpcDefinition> npcs;
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
    static bool ParseItemDefinitions(
        const std::filesystem::path& file_path,
        std::vector<ModItemDefinition>& out_items,
        std::string& out_error);
    static bool ParseRecipeDefinitions(
        const std::filesystem::path& file_path,
        std::vector<ModRecipeDefinition>& out_recipes,
        std::string& out_error);
    static bool ParseNpcDefinitions(
        const std::filesystem::path& file_path,
        std::vector<ModNpcDefinition>& out_npcs,
        std::string& out_error);
    static bool BuildDependencyOrderedManifestList(
        const std::vector<ModManifest>& loaded_manifests,
        std::vector<ModManifest>& out_ordered_manifests,
        std::string& out_error);

    bool initialized_ = false;
    std::filesystem::path mod_root_;
};

}  // namespace novaria::mod
