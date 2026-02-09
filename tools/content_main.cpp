#include "content/pak.h"
#include "core/logger.h"
#include "mod/mod_fingerprint.h"
#include "mod/mod_loader.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options final {
    std::string command;
    std::filesystem::path mods_root;
    std::filesystem::path out_dir;
};

void PrintUsage() {
    std::cout
        << "Usage:\n"
        << "  novaria_content validate --mods <path>\n"
        << "  novaria_content fingerprint --mods <path>\n"
        << "  novaria_content pack --mods <path> --out <path>\n";
}

bool ReadValue(
    int argc,
    char** argv,
    int& in_out_index,
    const char* option_name,
    std::string& out_value,
    std::string& out_error) {
    if (in_out_index + 1 >= argc) {
        out_error = std::string("Missing value for option: ") + option_name;
        return false;
    }
    ++in_out_index;
    out_value = argv[in_out_index];
    if (out_value.empty()) {
        out_error = std::string("Empty value for option: ") + option_name;
        return false;
    }
    return true;
}

bool ParseArguments(int argc, char** argv, Options& out_options, std::string& out_error) {
    if (argc < 2) {
        out_error = "Missing command.";
        return false;
    }

    out_options.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;
        if (arg == "--mods") {
            if (!ReadValue(argc, argv, i, "--mods", value, out_error)) {
                return false;
            }
            out_options.mods_root = value;
            continue;
        }
        if (arg == "--out") {
            if (!ReadValue(argc, argv, i, "--out", value, out_error)) {
                return false;
            }
            out_options.out_dir = value;
            continue;
        }

        out_error = "Unknown option: " + arg;
        return false;
    }

    if (out_options.mods_root.empty()) {
        out_error = "Missing required option: --mods";
        return false;
    }

    if (out_options.command == "pack" && out_options.out_dir.empty()) {
        out_error = "Missing required option for pack: --out";
        return false;
    }

    out_error.clear();
    return true;
}

bool ValidateMods(const std::filesystem::path& mods_root, std::string& out_error) {
    novaria::mod::ModLoader loader;
    if (!loader.Initialize(mods_root, out_error)) {
        return false;
    }

    std::vector<novaria::mod::ModManifest> manifests;
    if (!loader.LoadAll(manifests, out_error)) {
        loader.Shutdown();
        return false;
    }

    const auto core_iter =
        std::find_if(
            manifests.begin(),
            manifests.end(),
            [](const novaria::mod::ModManifest& manifest) { return manifest.name == "core"; });
    if (core_iter == manifests.end()) {
        out_error = "Required mod missing: core";
        loader.Shutdown();
        return false;
    }
    if (core_iter->script_entry.empty()) {
        out_error = "Required mod has no script_entry: core";
        loader.Shutdown();
        return false;
    }

    std::string fingerprint;
    if (!novaria::mod::BuildGameplayFingerprint(manifests, fingerprint, out_error)) {
        loader.Shutdown();
        return false;
    }

    loader.Shutdown();
    out_error.clear();
    return true;
}

bool FingerprintMods(const std::filesystem::path& mods_root, std::string& out_error) {
    novaria::mod::ModLoader loader;
    if (!loader.Initialize(mods_root, out_error)) {
        return false;
    }

    std::vector<novaria::mod::ModManifest> manifests;
    if (!loader.LoadAll(manifests, out_error)) {
        loader.Shutdown();
        return false;
    }

    const std::string manifest_fingerprint =
        novaria::mod::ModLoader::BuildManifestFingerprint(manifests);

    std::string gameplay_fingerprint;
    if (!novaria::mod::BuildGameplayFingerprint(manifests, gameplay_fingerprint, out_error)) {
        loader.Shutdown();
        return false;
    }

    std::cout << "manifest_fingerprint=" << manifest_fingerprint << "\n";
    std::cout << "gameplay_fingerprint=" << gameplay_fingerprint << "\n";

    loader.Shutdown();
    out_error.clear();
    return true;
}

bool ReadBinaryFile(
    const std::filesystem::path& file_path,
    std::vector<std::uint8_t>& out_bytes,
    std::string& out_error) {
    out_bytes.clear();
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        out_error = "cannot open file: " + file_path.string();
        return false;
    }

    file.unsetf(std::ios::skipws);
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        out_error = "cannot determine file size: " + file_path.string();
        return false;
    }
    if (static_cast<std::uint64_t>(size) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        out_error = "file too large: " + file_path.string();
        return false;
    }
    file.seekg(0, std::ios::beg);

    out_bytes.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(out_bytes.data()), size);
    if (!file) {
        out_error = "cannot read file: " + file_path.string();
        out_bytes.clear();
        return false;
    }

    out_error.clear();
    return true;
}

bool PackSingleModDirectory(
    const novaria::mod::ModManifest& manifest,
    const std::filesystem::path& out_dir,
    std::string& out_error) {
    if (manifest.container_kind != novaria::mod::ModContainerKind::Directory) {
        out_error = "pack only supports directory mods (found non-directory mod): " + manifest.name;
        return false;
    }

    const std::filesystem::path mod_dir = manifest.container_path;
    const std::filesystem::path manifest_path = mod_dir / "mod.cfg";
    const std::filesystem::path content_dir = mod_dir / "content";

    novaria::content::PakWriter writer;

    std::vector<std::uint8_t> bytes;
    if (!ReadBinaryFile(manifest_path, bytes, out_error)) {
        out_error = "Failed to read mod manifest file '" + manifest_path.string() + "': " + out_error;
        return false;
    }
    if (!writer.AddFile("mod.cfg", std::move(bytes), out_error)) {
        out_error = "Failed to add mod.cfg to pak: " + out_error;
        return false;
    }

    if (std::filesystem::exists(content_dir) && std::filesystem::is_directory(content_dir)) {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(content_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            files.push_back(entry.path());
        }

        std::sort(files.begin(), files.end());
        for (const std::filesystem::path& file_path : files) {
            const std::filesystem::path rel = std::filesystem::relative(file_path, mod_dir);
            const std::string pak_path = rel.generic_string();
            if (pak_path.empty()) {
                continue;
            }

            if (!ReadBinaryFile(file_path, bytes, out_error)) {
                out_error = "Failed to read mod content file '" + file_path.string() + "': " + out_error;
                return false;
            }
            if (!writer.AddFile(pak_path, std::move(bytes), out_error)) {
                out_error = "Failed to add file to pak: " + out_error;
                return false;
            }
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        out_error = "Failed to create output directory: " + ec.message();
        return false;
    }

    const std::filesystem::path out_pak_path = out_dir / (manifest.name + ".pak");
    if (!writer.WriteToFile(out_pak_path, out_error)) {
        out_error = "Failed to write pak '" + out_pak_path.string() + "': " + out_error;
        return false;
    }

    out_error.clear();
    return true;
}

bool PackMods(
    const std::filesystem::path& mods_root,
    const std::filesystem::path& out_dir,
    std::string& out_error) {
    novaria::mod::ModLoader loader;
    if (!loader.Initialize(mods_root, out_error)) {
        return false;
    }

    std::vector<novaria::mod::ModManifest> manifests;
    if (!loader.LoadAll(manifests, out_error)) {
        loader.Shutdown();
        return false;
    }

    for (const auto& manifest : manifests) {
        if (manifest.container_kind != novaria::mod::ModContainerKind::Directory) {
            continue;
        }

        if (!PackSingleModDirectory(manifest, out_dir, out_error)) {
            loader.Shutdown();
            return false;
        }
    }

    loader.Shutdown();
    out_error.clear();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options{};
    std::string error;
    if (!ParseArguments(argc, argv, options, error)) {
        std::cerr << "[ERROR] " << error << "\n";
        PrintUsage();
        return 1;
    }

    if (options.command == "validate") {
        if (!ValidateMods(options.mods_root, error)) {
            std::cerr << "[ERROR] " << error << "\n";
            return 1;
        }
        std::cout << "[OK] validate\n";
        return 0;
    }

    if (options.command == "fingerprint") {
        if (!FingerprintMods(options.mods_root, error)) {
            std::cerr << "[ERROR] " << error << "\n";
            return 1;
        }
        return 0;
    }

    if (options.command == "pack") {
        if (!PackMods(options.mods_root, options.out_dir, error)) {
            std::cerr << "[ERROR] " << error << "\n";
            return 1;
        }
        std::cout << "[OK] pack\n";
        return 0;
    }

    std::cerr << "[ERROR] Unknown command: " << options.command << "\n";
    PrintUsage();
    return 1;
}
