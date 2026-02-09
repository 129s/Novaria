#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace novaria::content {

struct PakEntry final {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::array<std::uint8_t, 32> sha256{};
};

std::string NormalizePakPath(std::string_view path);

class PakReader final {
public:
    bool Open(const std::filesystem::path& file_path, std::string& out_error);
    void Close();

    bool IsOpen() const;
    const std::filesystem::path& FilePath() const;

    bool Contains(std::string_view pak_path) const;
    bool ReadFile(
        std::string_view pak_path,
        std::vector<std::uint8_t>& out_bytes,
        std::string& out_error) const;
    bool ReadTextFile(
        std::string_view pak_path,
        std::string& out_text,
        std::string& out_error) const;

private:
    bool LoadIndex(std::string& out_error);

    std::filesystem::path file_path_;
    std::unordered_map<std::string, PakEntry> path_to_entry_;
};

class PakWriter final {
public:
    bool AddFile(
        std::string_view pak_path,
        std::vector<std::uint8_t> bytes,
        std::string& out_error);
    bool WriteToFile(const std::filesystem::path& file_path, std::string& out_error) const;

private:
    struct PendingFile final {
        std::string pak_path;
        std::vector<std::uint8_t> bytes;
        std::array<std::uint8_t, 32> sha256{};
    };

    std::vector<PendingFile> files_;
};

}  // namespace novaria::content

