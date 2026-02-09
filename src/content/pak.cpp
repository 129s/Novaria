#include "content/pak.h"

#include "content/binary_io.h"
#include "core/sha256.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

namespace novaria::content {
namespace {

constexpr char kPakMagic[8]{'N', 'V', 'P', 'A', 'K', 0, 0, 0};
constexpr std::uint32_t kPakVersion = 1;

bool IsSafeRelativePakPath(std::string_view path) {
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path.find(':') != std::string_view::npos) {
        return false;
    }

    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const std::size_t part_end = (end == std::string_view::npos) ? path.size() : end;
        const std::string_view part = path.substr(start, part_end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = (end == std::string_view::npos) ? path.size() : end + 1;
    }

    return true;
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

}  // namespace

std::string NormalizePakPath(std::string_view path) {
    std::string normalized;
    normalized.reserve(path.size());
    for (char ch : path) {
        if (ch == '\\') {
            normalized.push_back('/');
            continue;
        }
        normalized.push_back(ch);
    }

    while (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/') {
        normalized.erase(normalized.begin(), normalized.begin() + 2);
    }

    if (!IsSafeRelativePakPath(normalized)) {
        return {};
    }
    return normalized;
}

bool PakReader::Open(const std::filesystem::path& file_path, std::string& out_error) {
    Close();
    file_path_ = file_path;
    return LoadIndex(out_error);
}

void PakReader::Close() {
    file_path_.clear();
    path_to_entry_.clear();
}

bool PakReader::IsOpen() const {
    return !file_path_.empty();
}

const std::filesystem::path& PakReader::FilePath() const {
    return file_path_;
}

bool PakReader::Contains(std::string_view pak_path) const {
    const std::string normalized = NormalizePakPath(pak_path);
    if (normalized.empty()) {
        return false;
    }
    return path_to_entry_.find(normalized) != path_to_entry_.end();
}

bool PakReader::ReadFile(
    std::string_view pak_path,
    std::vector<std::uint8_t>& out_bytes,
    std::string& out_error) const {
    out_bytes.clear();
    if (!IsOpen()) {
        out_error = "Pak is not open.";
        return false;
    }

    const std::string normalized = NormalizePakPath(pak_path);
    if (normalized.empty()) {
        out_error = "Invalid pak path.";
        return false;
    }

    const auto it = path_to_entry_.find(normalized);
    if (it == path_to_entry_.end()) {
        out_error = "Pak entry not found: " + normalized;
        return false;
    }

    std::ifstream file(file_path_, std::ios::binary);
    if (!file.is_open()) {
        out_error = "Cannot open pak file: " + file_path_.string();
        return false;
    }

    const PakEntry& entry = it->second;
    file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    if (!file) {
        out_error = "Cannot seek pak entry: " + normalized;
        return false;
    }

    if (entry.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        out_error = "Pak entry too large: " + normalized;
        return false;
    }

    out_bytes.resize(static_cast<std::size_t>(entry.size));
    file.read(reinterpret_cast<char*>(out_bytes.data()), static_cast<std::streamsize>(out_bytes.size()));
    if (!file) {
        out_error = "Cannot read pak entry: " + normalized;
        out_bytes.clear();
        return false;
    }

    out_error.clear();
    return true;
}

bool PakReader::ReadTextFile(
    std::string_view pak_path,
    std::string& out_text,
    std::string& out_error) const {
    out_text.clear();
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(pak_path, bytes, out_error)) {
        return false;
    }

    out_text.assign(bytes.begin(), bytes.end());
    out_text = StripUtf8Bom(std::move(out_text));
    out_text = NormalizeTextNewlines(std::move(out_text));
    out_error.clear();
    return true;
}

bool PakReader::LoadIndex(std::string& out_error) {
    path_to_entry_.clear();

    std::ifstream file(file_path_, std::ios::binary);
    if (!file.is_open()) {
        out_error = "Cannot open pak file: " + file_path_.string();
        return false;
    }

    char magic[8]{};
    file.read(magic, sizeof(magic));
    if (!file || std::memcmp(magic, kPakMagic, sizeof(kPakMagic)) != 0) {
        out_error = "Invalid pak magic: " + file_path_.string();
        return false;
    }

    std::uint32_t version = 0;
    if (!detail::ReadU32Le(file, version) || version != kPakVersion) {
        out_error = "Unsupported pak version: " + file_path_.string();
        return false;
    }

    std::uint64_t index_offset = 0;
    std::uint64_t index_size = 0;
    if (!detail::ReadU64Le(file, index_offset) || !detail::ReadU64Le(file, index_size)) {
        out_error = "Invalid pak header: " + file_path_.string();
        return false;
    }

    file.seekg(static_cast<std::streamoff>(index_offset), std::ios::beg);
    if (!file) {
        out_error = "Cannot seek pak index: " + file_path_.string();
        return false;
    }

    std::uint32_t entry_count = 0;
    if (!detail::ReadU32Le(file, entry_count)) {
        out_error = "Cannot read pak index entry_count: " + file_path_.string();
        return false;
    }

    path_to_entry_.reserve(entry_count);
    for (std::uint32_t i = 0; i < entry_count; ++i) {
        std::uint32_t path_len = 0;
        if (!detail::ReadU32Le(file, path_len) || path_len == 0 || path_len > (1U << 20)) {
            out_error = "Invalid pak index path length: " + file_path_.string();
            return false;
        }

        std::string path;
        path.resize(path_len);
        file.read(path.data(), static_cast<std::streamsize>(path.size()));
        if (!file) {
            out_error = "Cannot read pak index path: " + file_path_.string();
            return false;
        }

        const std::string normalized = NormalizePakPath(path);
        if (normalized.empty()) {
            out_error = "Invalid pak index path: " + file_path_.string();
            return false;
        }

        PakEntry entry{};
        if (!detail::ReadU64Le(file, entry.offset) || !detail::ReadU64Le(file, entry.size)) {
            out_error = "Cannot read pak index entry header: " + file_path_.string();
            return false;
        }

        file.read(reinterpret_cast<char*>(entry.sha256.data()), static_cast<std::streamsize>(entry.sha256.size()));
        if (!file) {
            out_error = "Cannot read pak index sha256: " + file_path_.string();
            return false;
        }

        const auto [it, inserted] = path_to_entry_.emplace(normalized, entry);
        if (!inserted) {
            out_error = "Duplicate pak entry path: " + normalized;
            return false;
        }
    }

    out_error.clear();
    return true;
}

bool PakWriter::AddFile(
    std::string_view pak_path,
    std::vector<std::uint8_t> bytes,
    std::string& out_error) {
    const std::string normalized = NormalizePakPath(pak_path);
    if (normalized.empty()) {
        out_error = "Invalid pak path.";
        return false;
    }

    for (const auto& existing : files_) {
        if (existing.pak_path == normalized) {
            out_error = "Duplicate pak path: " + normalized;
            return false;
        }
    }

    core::Sha256 hasher;
    hasher.Update(std::string_view(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()));
    PendingFile pending{
        .pak_path = normalized,
        .bytes = std::move(bytes),
    };
    std::uint8_t digest[32]{};
    hasher.Finalize(digest);
    std::copy(std::begin(digest), std::end(digest), pending.sha256.begin());

    files_.push_back(std::move(pending));
    out_error.clear();
    return true;
}

bool PakWriter::WriteToFile(const std::filesystem::path& file_path, std::string& out_error) const {
    if (files_.empty()) {
        out_error = "No files to write.";
        return false;
    }

    std::vector<const PendingFile*> ordered;
    ordered.reserve(files_.size());
    for (const auto& file : files_) {
        ordered.push_back(&file);
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const PendingFile* lhs, const PendingFile* rhs) { return lhs->pak_path < rhs->pak_path; });

    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        out_error = "Cannot open output pak file: " + file_path.string();
        return false;
    }

    out.write(kPakMagic, sizeof(kPakMagic));
    detail::WriteU32Le(out, kPakVersion);
    detail::WriteU64Le(out, 0);
    detail::WriteU64Le(out, 0);

    struct IndexEntry final {
        std::string path;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::array<std::uint8_t, 32> sha256{};
    };
    std::vector<IndexEntry> index_entries;
    index_entries.reserve(ordered.size());

    for (const PendingFile* pending : ordered) {
        const std::uint64_t offset = static_cast<std::uint64_t>(out.tellp());
        out.write(
            reinterpret_cast<const char*>(pending->bytes.data()),
            static_cast<std::streamsize>(pending->bytes.size()));
        if (!out) {
            out_error = "Failed to write pak payload.";
            return false;
        }

        index_entries.push_back(IndexEntry{
            .path = pending->pak_path,
            .offset = offset,
            .size = static_cast<std::uint64_t>(pending->bytes.size()),
            .sha256 = pending->sha256,
        });
    }

    const std::uint64_t index_offset = static_cast<std::uint64_t>(out.tellp());
    detail::WriteU32Le(out, static_cast<std::uint32_t>(index_entries.size()));
    for (const IndexEntry& entry : index_entries) {
        if (entry.path.size() > (1U << 20)) {
            out_error = "Pak path too long.";
            return false;
        }

        detail::WriteU32Le(out, static_cast<std::uint32_t>(entry.path.size()));
        out.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
        detail::WriteU64Le(out, entry.offset);
        detail::WriteU64Le(out, entry.size);
        out.write(reinterpret_cast<const char*>(entry.sha256.data()), static_cast<std::streamsize>(entry.sha256.size()));
        if (!out) {
            out_error = "Failed to write pak index.";
            return false;
        }
    }
    const std::uint64_t index_end = static_cast<std::uint64_t>(out.tellp());
    const std::uint64_t index_size = index_end - index_offset;

    out.seekp(static_cast<std::streamoff>(sizeof(kPakMagic) + sizeof(std::uint32_t)), std::ios::beg);
    detail::WriteU64Le(out, index_offset);
    detail::WriteU64Le(out, index_size);
    if (!out) {
        out_error = "Failed to finalize pak header.";
        return false;
    }

    out_error.clear();
    return true;
}

}  // namespace novaria::content
