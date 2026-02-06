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

bool ParsePort(const std::string& value, int& out_port) {
    int parsed_port = 0;
    if (!ParseInt(value, parsed_port)) {
        return false;
    }

    if (parsed_port < 0 || parsed_port > 65535) {
        return false;
    }

    out_port = parsed_port;
    return true;
}

bool ParseString(const std::string& value, std::string& out_value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return false;
    }
    out_value = value.substr(1, value.size() - 2);
    return true;
}

bool ParseScriptBackendMode(const std::string& value, ScriptBackendMode& out_mode) {
    std::string parsed;
    if (!ParseString(value, parsed)) {
        return false;
    }

    if (parsed == "auto") {
        out_mode = ScriptBackendMode::Auto;
        return true;
    }

    if (parsed == "stub") {
        out_mode = ScriptBackendMode::Stub;
        return true;
    }

    if (parsed == "luajit") {
        out_mode = ScriptBackendMode::LuaJit;
        return true;
    }

    return false;
}

bool ParseNetBackendMode(const std::string& value, NetBackendMode& out_mode) {
    std::string parsed;
    if (!ParseString(value, parsed)) {
        return false;
    }

    if (parsed == "auto") {
        out_mode = NetBackendMode::Auto;
        return true;
    }

    if (parsed == "stub") {
        out_mode = NetBackendMode::Stub;
        return true;
    }

    if (parsed == "udp_loopback") {
        out_mode = NetBackendMode::UdpLoopback;
        return true;
    }

    return false;
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

        if (key == "strict_save_mod_fingerprint") {
            if (!ParseBool(value, out_config.strict_save_mod_fingerprint)) {
                out_error =
                    "strict_save_mod_fingerprint expects boolean: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "script_backend") {
            if (!ParseScriptBackendMode(value, out_config.script_backend_mode)) {
                out_error =
                    "script_backend expects one of \"auto\"|\"stub\"|\"luajit\": line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_backend") {
            if (!ParseNetBackendMode(value, out_config.net_backend_mode)) {
                out_error =
                    "net_backend expects one of \"auto\"|\"stub\"|\"udp_loopback\": line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_udp_local_port") {
            if (!ParsePort(value, out_config.net_udp_local_port)) {
                out_error = "net_udp_local_port expects integer within [0,65535]: line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_udp_remote_host") {
            if (!ParseString(value, out_config.net_udp_remote_host)) {
                out_error = "net_udp_remote_host expects string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_udp_remote_port") {
            if (!ParsePort(value, out_config.net_udp_remote_port)) {
                out_error = "net_udp_remote_port expects integer within [0,65535]: line " +
                    std::to_string(line_number);
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

const char* ScriptBackendModeName(ScriptBackendMode mode) {
    switch (mode) {
        case ScriptBackendMode::Auto:
            return "auto";
        case ScriptBackendMode::Stub:
            return "stub";
        case ScriptBackendMode::LuaJit:
            return "luajit";
    }

    return "unknown";
}

const char* NetBackendModeName(NetBackendMode mode) {
    switch (mode) {
        case NetBackendMode::Auto:
            return "auto";
        case NetBackendMode::Stub:
            return "stub";
        case NetBackendMode::UdpLoopback:
            return "udp_loopback";
    }

    return "unknown";
}

}  // namespace novaria::core
