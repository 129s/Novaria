#include "mod/mod_loader.h"
#include "content/pak.h"
#include "core/sha256.h"
#include "core/cfg_parser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <functional>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace novaria::mod {
namespace {

bool HasPakExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".pak";
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream line_stream(line);
    while (std::getline(line_stream, token, ',')) {
        tokens.push_back(token);
    }
    return tokens;
}

bool ParseUInt32Token(const std::string& token, std::uint32_t& out_value) {
    if (token.empty()) {
        return false;
    }

    std::uint64_t parsed = 0;
    const auto [parse_end, error] =
        std::from_chars(token.data(), token.data() + token.size(), parsed);
    if (error != std::errc{} || parse_end != token.data() + token.size()) {
        return false;
    }
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    out_value = static_cast<std::uint32_t>(parsed);
    return true;
}

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

bool ParseManifestLines(
    const std::vector<core::cfg::KeyValueLine>& lines,
    ModManifest& out_manifest,
    std::string& out_error) {
    for (const core::cfg::KeyValueLine& line : lines) {
        const std::string& key = line.key;
        const std::string& value = line.value;
        const int line_number = line.line_number;

        if (key == "name") {
            if (!core::cfg::ParseQuotedString(value, out_manifest.name)) {
                out_error = "name must be quoted string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "version") {
            if (!core::cfg::ParseQuotedString(value, out_manifest.version)) {
                out_error = "version must be quoted string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "description") {
            if (!core::cfg::ParseQuotedString(value, out_manifest.description)) {
                out_error = "description must be quoted string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "dependencies") {
            if (!core::cfg::ParseQuotedStringArray(value, out_manifest.dependencies)) {
                out_error = "dependencies must be quoted string array: line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "script_entry") {
            if (!core::cfg::ParseQuotedString(value, out_manifest.script_entry)) {
                out_error = "script_entry must be quoted string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "script_api_version") {
            if (!core::cfg::ParseQuotedString(value, out_manifest.script_api_version)) {
                out_error = "script_api_version must be quoted string: line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "script_capabilities") {
            if (!core::cfg::ParseQuotedStringArray(value, out_manifest.script_capabilities)) {
                out_error = "script_capabilities must be quoted string array: line " +
                    std::to_string(line_number);
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

    out_error.clear();
    return true;
}

}  // namespace

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

        if (entry.is_directory()) {
            const std::filesystem::path manifest_path = entry.path() / "mod.cfg";
            if (!std::filesystem::exists(manifest_path)) {
                continue;
            }

            ModManifest manifest{};
            if (!ParseManifestFile(manifest_path, manifest, out_error)) {
                out_error = "Invalid mod manifest '" + manifest_path.string() + "': " + out_error;
                return false;
            }

            manifest.container_kind = ModContainerKind::Directory;
            manifest.container_path = entry.path();

            const std::filesystem::path content_root = entry.path() / "content";
            if (std::filesystem::exists(content_root) && std::filesystem::is_directory(content_root)) {
                const std::filesystem::path items_path = content_root / "items.csv";
                if (std::filesystem::exists(items_path) &&
                    !ParseItemDefinitions(items_path, manifest.items, out_error)) {
                    out_error = "Invalid mod items file '" + items_path.string() + "': " + out_error;
                    return false;
                }

                const std::filesystem::path recipes_path = content_root / "recipes.csv";
                if (std::filesystem::exists(recipes_path) &&
                    !ParseRecipeDefinitions(recipes_path, manifest.recipes, out_error)) {
                    out_error = "Invalid mod recipes file '" + recipes_path.string() + "': " + out_error;
                    return false;
                }

                const std::filesystem::path npcs_path = content_root / "npcs.csv";
                if (std::filesystem::exists(npcs_path) &&
                    !ParseNpcDefinitions(npcs_path, manifest.npcs, out_error)) {
                    out_error = "Invalid mod npcs file '" + npcs_path.string() + "': " + out_error;
                    return false;
                }
            }

            loaded_manifests.push_back(std::move(manifest));
            continue;
        }

        if (entry.is_regular_file() && HasPakExtension(entry.path())) {
            content::PakReader pak;
            if (!pak.Open(entry.path(), out_error)) {
                out_error = "Invalid mod pak '" + entry.path().string() + "': " + out_error;
                return false;
            }

            std::string manifest_text;
            if (!pak.ReadTextFile("mod.cfg", manifest_text, out_error)) {
                out_error = "Invalid mod pak '" + entry.path().string() + "': " + out_error;
                return false;
            }

            std::vector<core::cfg::KeyValueLine> lines;
            if (!core::cfg::ParseText(manifest_text, lines, out_error)) {
                out_error = "Invalid mod pak manifest 'mod.cfg' in '" + entry.path().string() + "': " + out_error;
                return false;
            }

            ModManifest manifest{};
            if (!ParseManifestLines(lines, manifest, out_error)) {
                out_error = "Invalid mod pak manifest 'mod.cfg' in '" + entry.path().string() + "': " + out_error;
                return false;
            }

            manifest.container_kind = ModContainerKind::Pak;
            manifest.container_path = entry.path();

            std::string items_text;
            if (pak.Contains("content/items.csv") &&
                !pak.ReadTextFile("content/items.csv", items_text, out_error)) {
                out_error = "Invalid mod items file 'content/items.csv' in pak '" + entry.path().string() + "': " + out_error;
                return false;
            }
            if (!items_text.empty() &&
                !ParseItemDefinitionsText(items_text, manifest.items, out_error)) {
                out_error = "Invalid mod items file 'content/items.csv' in pak '" + entry.path().string() + "': " + out_error;
                return false;
            }

            std::string recipes_text;
            if (pak.Contains("content/recipes.csv") &&
                !pak.ReadTextFile("content/recipes.csv", recipes_text, out_error)) {
                out_error = "Invalid mod recipes file 'content/recipes.csv' in pak '" + entry.path().string() + "': " + out_error;
                return false;
            }
            if (!recipes_text.empty() &&
                !ParseRecipeDefinitionsText(recipes_text, manifest.recipes, out_error)) {
                out_error = "Invalid mod recipes file 'content/recipes.csv' in pak '" + entry.path().string() + "': " + out_error;
                return false;
            }

            std::string npcs_text;
            if (pak.Contains("content/npcs.csv") &&
                !pak.ReadTextFile("content/npcs.csv", npcs_text, out_error)) {
                out_error = "Invalid mod npcs file 'content/npcs.csv' in pak '" + entry.path().string() + "': " + out_error;
                return false;
            }
            if (!npcs_text.empty() &&
                !ParseNpcDefinitionsText(npcs_text, manifest.npcs, out_error)) {
                out_error = "Invalid mod npcs file 'content/npcs.csv' in pak '" + entry.path().string() + "': " + out_error;
                return false;
            }

            loaded_manifests.push_back(std::move(manifest));
        }
    }

    return BuildDependencyOrderedManifestList(loaded_manifests, out_manifests, out_error);
}

std::string ModLoader::BuildManifestFingerprint(const std::vector<ModManifest>& manifests) {
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

        std::string canonical_entry;
        canonical_entry.reserve(256);
        AppendLengthPrefixedField(canonical_entry, "name", manifest.name);
        AppendLengthPrefixedField(canonical_entry, "version", manifest.version);
        AppendLengthPrefixedField(canonical_entry, "description", manifest.description);
        AppendLengthPrefixedField(canonical_entry, "script_entry", manifest.script_entry);
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
    canonical_payload.reserve(canonical_entries.size() * 128);
    for (const auto& entry : canonical_entries) {
        canonical_payload += entry;
        canonical_payload.push_back('\n');
    }

    return core::Sha256::HexDigest(canonical_payload);
}

std::string ModLoader::Trim(const std::string& value) {
    return core::cfg::Trim(value);
}

bool ModLoader::ParseItemDefinitions(
    const std::filesystem::path& file_path,
    std::vector<ModItemDefinition>& out_items,
    std::string& out_error) {
    out_items.clear();

    std::ifstream file(file_path);
    if (!file.is_open()) {
        out_error = "cannot open file";
        return false;
    }

    return ParseItemDefinitionsStream(file, out_items, out_error);
}

bool ModLoader::ParseItemDefinitionsText(
    const std::string& text,
    std::vector<ModItemDefinition>& out_items,
    std::string& out_error) {
    out_items.clear();
    std::istringstream stream(text);
    return ParseItemDefinitionsStream(stream, out_items, out_error);
}

bool ModLoader::ParseItemDefinitionsStream(
    std::istream& file,
    std::vector<ModItemDefinition>& out_items,
    std::string& out_error) {
    std::unordered_map<std::string, bool> item_ids;
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto tokens = SplitCsvLine(line);
        if (tokens.size() != 2) {
            out_error = "line " + std::to_string(line_number) + " expects 2 csv tokens";
            return false;
        }

        const std::string item_id = Trim(tokens[0]);
        const std::string behavior = Trim(tokens[1]);
        if (item_id.empty() || behavior.empty()) {
            out_error = "line " + std::to_string(line_number) + " has empty item fields";
            return false;
        }
        if (item_ids.contains(item_id)) {
            out_error = "line " + std::to_string(line_number) + " duplicates item id '" + item_id + "'";
            return false;
        }

        item_ids.emplace(item_id, true);
        out_items.push_back(ModItemDefinition{
            .id = item_id,
            .behavior = behavior,
        });
    }

    out_error.clear();
    return true;
}

bool ModLoader::ParseRecipeDefinitions(
    const std::filesystem::path& file_path,
    std::vector<ModRecipeDefinition>& out_recipes,
    std::string& out_error) {
    out_recipes.clear();

    std::ifstream file(file_path);
    if (!file.is_open()) {
        out_error = "cannot open file";
        return false;
    }

    return ParseRecipeDefinitionsStream(file, out_recipes, out_error);
}

bool ModLoader::ParseRecipeDefinitionsText(
    const std::string& text,
    std::vector<ModRecipeDefinition>& out_recipes,
    std::string& out_error) {
    out_recipes.clear();
    std::istringstream stream(text);
    return ParseRecipeDefinitionsStream(stream, out_recipes, out_error);
}

bool ModLoader::ParseRecipeDefinitionsStream(
    std::istream& file,
    std::vector<ModRecipeDefinition>& out_recipes,
    std::string& out_error) {
    std::unordered_map<std::string, bool> recipe_ids;
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto tokens = SplitCsvLine(line);
        if (tokens.size() != 5) {
            out_error = "line " + std::to_string(line_number) + " expects 5 csv tokens";
            return false;
        }

        const std::string recipe_id = Trim(tokens[0]);
        const std::string input_item_id = Trim(tokens[1]);
        const std::string output_item_id = Trim(tokens[3]);
        std::uint32_t input_amount = 0;
        std::uint32_t output_amount = 0;
        if (recipe_id.empty() || input_item_id.empty() || output_item_id.empty()) {
            out_error = "line " + std::to_string(line_number) + " has empty recipe fields";
            return false;
        }
        if (!ParseUInt32Token(Trim(tokens[2]), input_amount) ||
            !ParseUInt32Token(Trim(tokens[4]), output_amount) ||
            input_amount == 0 ||
            output_amount == 0) {
            out_error = "line " + std::to_string(line_number) + " has invalid recipe amounts";
            return false;
        }
        if (recipe_ids.contains(recipe_id)) {
            out_error = "line " + std::to_string(line_number) + " duplicates recipe id '" + recipe_id + "'";
            return false;
        }

        recipe_ids.emplace(recipe_id, true);
        out_recipes.push_back(ModRecipeDefinition{
            .id = recipe_id,
            .input_item_id = input_item_id,
            .input_amount = input_amount,
            .output_item_id = output_item_id,
            .output_amount = output_amount,
        });
    }

    out_error.clear();
    return true;
}

bool ModLoader::ParseNpcDefinitions(
    const std::filesystem::path& file_path,
    std::vector<ModNpcDefinition>& out_npcs,
    std::string& out_error) {
    out_npcs.clear();

    std::ifstream file(file_path);
    if (!file.is_open()) {
        out_error = "cannot open file";
        return false;
    }

    return ParseNpcDefinitionsStream(file, out_npcs, out_error);
}

bool ModLoader::ParseNpcDefinitionsText(
    const std::string& text,
    std::vector<ModNpcDefinition>& out_npcs,
    std::string& out_error) {
    out_npcs.clear();
    std::istringstream stream(text);
    return ParseNpcDefinitionsStream(stream, out_npcs, out_error);
}

bool ModLoader::ParseNpcDefinitionsStream(
    std::istream& file,
    std::vector<ModNpcDefinition>& out_npcs,
    std::string& out_error) {
    std::unordered_map<std::string, bool> npc_ids;
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto tokens = SplitCsvLine(line);
        if (tokens.size() != 3) {
            out_error = "line " + std::to_string(line_number) + " expects 3 csv tokens";
            return false;
        }

        const std::string npc_id = Trim(tokens[0]);
        const std::string behavior = Trim(tokens[2]);
        std::uint32_t max_health = 0;
        if (npc_id.empty() || behavior.empty()) {
            out_error = "line " + std::to_string(line_number) + " has empty npc fields";
            return false;
        }
        if (!ParseUInt32Token(Trim(tokens[1]), max_health) || max_health == 0) {
            out_error = "line " + std::to_string(line_number) + " has invalid npc max_health";
            return false;
        }
        if (npc_ids.contains(npc_id)) {
            out_error = "line " + std::to_string(line_number) + " duplicates npc id '" + npc_id + "'";
            return false;
        }

        npc_ids.emplace(npc_id, true);
        out_npcs.push_back(ModNpcDefinition{
            .id = npc_id,
            .max_health = max_health,
            .behavior = behavior,
        });
    }

    out_error.clear();
    return true;
}

bool ModLoader::ParseManifestFile(
    const std::filesystem::path& manifest_path,
    ModManifest& out_manifest,
    std::string& out_error) {
    std::vector<core::cfg::KeyValueLine> lines;
    if (!core::cfg::ParseFile(manifest_path, lines, out_error)) {
        return false;
    }

    return ParseManifestLines(lines, out_manifest, out_error);
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
