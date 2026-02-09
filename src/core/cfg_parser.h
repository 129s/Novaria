#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace novaria::core::cfg {

struct KeyValueLine final {
    std::string key;
    std::string value;
    int line_number = 0;
};

bool ParseFile(
    const std::filesystem::path& file_path,
    std::vector<KeyValueLine>& out_lines,
    std::string& out_error);

std::string Trim(std::string_view text);

bool ParseQuotedString(std::string_view value, std::string& out_text);
bool ParseBool(std::string_view value, bool& out_value);
bool ParseInt(std::string_view value, int& out_value);
bool ParseQuotedStringArray(std::string_view value, std::vector<std::string>& out_items);

}  // namespace novaria::core::cfg

