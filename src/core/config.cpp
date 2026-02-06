#include "core/config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace novaria::core {
namespace {

std::string Trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool ParseBool(const std::string& value, bool& out_value) {
    if (value == "true") {
        out_value = true;
        return true;
    }
    if (value == "false") {
        out_value = false;
        return true;
    }
    return false;
}

bool ParseInt(const std::string& value, int& out_value) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            return false;
        }
        out_value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseString(const std::string& value, std::string& out_value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return false;
    }
    out_value = value.substr(1, value.size() - 2);
    return true;
}

}  // namespace

bool ConfigLoader::Load(
    const std::filesystem::path& file_path,
    GameConfig& out_config,
    std::string& out_error) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        out_error = "Cannot open config file: " + file_path.string();
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;

        const std::string::size_type comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        line = Trim(line);
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

        const std::string key = Trim(line.substr(0, equal_pos));
        const std::string value = Trim(line.substr(equal_pos + 1));

        if (key == "window_title") {
            if (!ParseString(value, out_config.window_title)) {
                out_error = "window_title expects string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "window_width") {
            if (!ParseInt(value, out_config.window_width)) {
                out_error = "window_width expects integer: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "window_height") {
            if (!ParseInt(value, out_config.window_height)) {
                out_error = "window_height expects integer: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "vsync") {
            if (!ParseBool(value, out_config.vsync)) {
                out_error = "vsync expects boolean: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }
    }

    if (out_config.window_width <= 0 || out_config.window_height <= 0) {
        out_error = "Window size must be greater than zero.";
        return false;
    }

    out_error.clear();
    return true;
}

}  // namespace novaria::core
