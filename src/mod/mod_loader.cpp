#include "mod/mod_loader.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace novaria::mod {
namespace {

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
    }

    return BuildDependencyOrderedManifestList(loaded_manifests, out_manifests, out_error);
}

std::string ModLoader::BuildManifestFingerprint(const std::vector<ModManifest>& manifests) {
    std::vector<std::string> canonical_entries;
    canonical_entries.reserve(manifests.size());
    for (const auto& manifest : manifests) {
        std::vector<std::string> normalized_dependencies = manifest.dependencies;
        std::sort(normalized_dependencies.begin(), normalized_dependencies.end());

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
        canonical_entry += "|items=";
        for (std::size_t item_index = 0; item_index < normalized_items.size(); ++item_index) {
            if (item_index > 0) {
                canonical_entry += ",";
            }
            canonical_entry += normalized_items[item_index];
        }
        canonical_entry += "|recipes=";
        for (std::size_t recipe_index = 0; recipe_index < normalized_recipes.size(); ++recipe_index) {
            if (recipe_index > 0) {
                canonical_entry += ",";
            }
            canonical_entry += normalized_recipes[recipe_index];
        }
        canonical_entry += "|npcs=";
        for (std::size_t npc_index = 0; npc_index < normalized_npcs.size(); ++npc_index) {
            if (npc_index > 0) {
                canonical_entry += ",";
            }
            canonical_entry += normalized_npcs[npc_index];
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
