#include "core/cfg_parser.h"

#include <cctype>
#include <fstream>

namespace novaria::core::cfg {
namespace {

std::string StripComment(std::string line) {
    const std::string::size_type comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
        line.erase(comment_pos);
    }
    return line;
}

}  // namespace

std::string Trim(std::string_view text) {
    auto is_not_space = [](unsigned char ch) { return !std::isspace(ch); };

    std::size_t start = 0;
    while (start < text.size() && !is_not_space(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && !is_not_space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

bool ParseFile(
    const std::filesystem::path& file_path,
    std::vector<KeyValueLine>& out_lines,
    std::string& out_error) {
    out_lines.clear();

    std::ifstream file(file_path);
    if (!file.is_open()) {
        out_error = "Cannot open config file: " + file_path.string();
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        line = Trim(StripComment(std::move(line)));
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            continue;
        }

        const std::string::size_type equal_pos = line.find('=');
        if (equal_pos == std::string::npos) {
            out_error = "Invalid config line (missing '='): line " + std::to_string(line_number);
            return false;
        }

        KeyValueLine parsed{};
        parsed.key = Trim(std::string_view(line).substr(0, equal_pos));
        parsed.value = Trim(std::string_view(line).substr(equal_pos + 1));
        parsed.line_number = line_number;
        if (parsed.key.empty()) {
            out_error = "Invalid config line (empty key): line " + std::to_string(line_number);
            return false;
        }

        out_lines.push_back(std::move(parsed));
    }

    out_error.clear();
    return true;
}

bool ParseBool(std::string_view value, bool& out_value) {
    const std::string trimmed = Trim(value);
    if (trimmed == "true") {
        out_value = true;
        return true;
    }
    if (trimmed == "false") {
        out_value = false;
        return true;
    }
    return false;
}

bool ParseInt(std::string_view value, int& out_value) {
    const std::string trimmed = Trim(value);
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(trimmed, &consumed);
        if (consumed != trimmed.size()) {
            return false;
        }
        out_value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseQuotedString(std::string_view value, std::string& out_text) {
    const std::string trimmed = Trim(value);
    if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"') {
        return false;
    }
    out_text = trimmed.substr(1, trimmed.size() - 2);
    return true;
}

bool ParseQuotedStringArray(std::string_view value, std::vector<std::string>& out_items) {
    out_items.clear();

    const std::string trimmed = Trim(value);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        return false;
    }

    std::string inner = Trim(std::string_view(trimmed).substr(1, trimmed.size() - 2));
    if (inner.empty()) {
        return true;
    }

    std::size_t offset = 0;
    while (offset < inner.size()) {
        while (offset < inner.size() &&
               std::isspace(static_cast<unsigned char>(inner[offset])) != 0) {
            ++offset;
        }
        if (offset >= inner.size()) {
            break;
        }

        if (inner[offset] != '"') {
            return false;
        }
        ++offset;

        const std::size_t start = offset;
        const std::size_t end_quote = inner.find('"', start);
        if (end_quote == std::string::npos) {
            return false;
        }

        out_items.push_back(inner.substr(start, end_quote - start));
        offset = end_quote + 1;

        while (offset < inner.size() &&
               std::isspace(static_cast<unsigned char>(inner[offset])) != 0) {
            ++offset;
        }
        if (offset >= inner.size()) {
            break;
        }
        if (inner[offset] != ',') {
            return false;
        }
        ++offset;
    }

    return true;
}

}  // namespace novaria::core::cfg

