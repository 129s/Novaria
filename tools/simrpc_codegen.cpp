#include "script/sim_rules_schema.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr const char* kBeginMarker = "-- BEGIN NOVARIA_SIMRPC_CONSTANTS (GENERATED)";
constexpr const char* kEndMarker = "-- END NOVARIA_SIMRPC_CONSTANTS (GENERATED)";

template <typename Enum>
int ToInt(Enum value) {
    return static_cast<int>(value);
}

bool ReadTextFile(const std::filesystem::path& path, std::string& out_text) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out_text = buffer.str();
    return true;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

std::string GenerateConstantsBlock() {
    using novaria::script::simrpc::ActionPrimaryResult;
    using novaria::script::simrpc::Command;
    using novaria::script::simrpc::CraftRecipeResult;
    using novaria::script::simrpc::CraftedKind;
    using novaria::script::simrpc::PlaceKind;
    using novaria::script::simrpc::kVersion;

    std::ostringstream out;
    out << kBeginMarker << "\n";
    out << "local RPC_VERSION = " << static_cast<int>(kVersion) << "\n\n";

    out << "local CMD_VALIDATE = " << ToInt(Command::Validate) << "\n";
    out << "local CMD_ACTION_PRIMARY = " << ToInt(Command::GameplayActionPrimary) << "\n";
    out << "local CMD_CRAFT_RECIPE = " << ToInt(Command::GameplayCraftRecipe) << "\n\n";

    out << "local ACTION_REJECT = " << ToInt(ActionPrimaryResult::Reject) << "\n";
    out << "local ACTION_HARVEST = " << ToInt(ActionPrimaryResult::Harvest) << "\n";
    out << "local ACTION_PLACE = " << ToInt(ActionPrimaryResult::Place) << "\n\n";

    out << "local PLACE_NONE = " << ToInt(PlaceKind::None) << "\n";
    out << "local PLACE_DIRT = " << ToInt(PlaceKind::Dirt) << "\n";
    out << "local PLACE_STONE = " << ToInt(PlaceKind::Stone) << "\n";
    out << "local PLACE_TORCH = " << ToInt(PlaceKind::Torch) << "\n";
    out << "local PLACE_WORKBENCH = " << ToInt(PlaceKind::Workbench) << "\n\n";

    out << "local CRAFT_REJECT = " << ToInt(CraftRecipeResult::Reject) << "\n";
    out << "local CRAFT_CRAFT = " << ToInt(CraftRecipeResult::Craft) << "\n\n";

    out << "local CRAFTED_NONE = " << ToInt(CraftedKind::None) << "\n";
    out << "local CRAFTED_WORKBENCH = " << ToInt(CraftedKind::Workbench) << "\n";
    out << "local CRAFTED_TORCH = " << ToInt(CraftedKind::Torch) << "\n";
    out << kEndMarker << "\n";
    return out.str();
}

bool ReplaceGeneratedBlock(std::string& in_out_text, std::string& out_error) {
    const std::string begin = kBeginMarker;
    const std::string end = kEndMarker;
    const std::size_t begin_pos = in_out_text.find(begin);
    if (begin_pos == std::string::npos) {
        out_error = "Begin marker not found.";
        return false;
    }
    const std::size_t end_pos = in_out_text.find(end, begin_pos);
    if (end_pos == std::string::npos) {
        out_error = "End marker not found.";
        return false;
    }

    const std::size_t end_line_pos = in_out_text.find('\n', end_pos);
    const std::size_t replace_end = end_line_pos == std::string::npos ? in_out_text.size() : (end_line_pos + 1);

    const std::string block = GenerateConstantsBlock();
    in_out_text.replace(begin_pos, replace_end - begin_pos, block);
    out_error.clear();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path core_lua_path = "mods/core/content/scripts/core.lua";
    if (argc == 3 && std::string(argv[1]) == "--core-lua") {
        core_lua_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: novaria_simrpc_codegen [--core-lua <path>]\n";
        return 2;
    }

    std::string text;
    if (!ReadTextFile(core_lua_path, text)) {
        std::cerr << "Failed to read: " << core_lua_path.string() << "\n";
        return 1;
    }

    std::string error;
    if (!ReplaceGeneratedBlock(text, error)) {
        std::cerr << "Failed to update core.lua: " << error << "\n";
        return 1;
    }

    if (!WriteTextFile(core_lua_path, text)) {
        std::cerr << "Failed to write: " << core_lua_path.string() << "\n";
        return 1;
    }

    std::cout << "[OK] Updated simrpc constants block: " << core_lua_path.string() << "\n";
    return 0;
}

