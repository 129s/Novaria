#include "core/config.h"
#include "core/logger.h"
#include "core/cfg_parser.h"
#include "default_config_embedded.h"

#include <algorithm>
#include <string>
#include <vector>

namespace novaria::core {
namespace {

bool ParsePort(const std::string& value, int& out_port) {
    int parsed_port = 0;
    if (!cfg::ParseInt(value, parsed_port)) {
        return false;
    }

    if (parsed_port < 0 || parsed_port > 65535) {
        return false;
    }

    out_port = parsed_port;
    return true;
}

bool ParseString(const std::string& value, std::string& out_value) {
    return cfg::ParseQuotedString(value, out_value);
}

bool ApplyConfigLines(
    const std::vector<cfg::KeyValueLine>& lines,
    GameConfig& in_out_config,
    std::string& out_error) {
    for (const cfg::KeyValueLine& line : lines) {
        const std::string& key = line.key;
        const std::string& value = line.value;
        const int line_number = line.line_number;

        if (key == "window_title") {
            if (!ParseString(value, in_out_config.window_title)) {
                out_error = "window_title expects string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "window_width") {
            if (!cfg::ParseInt(value, in_out_config.window_width)) {
                out_error = "window_width expects integer: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "window_height") {
            if (!cfg::ParseInt(value, in_out_config.window_height)) {
                out_error = "window_height expects integer: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "vsync") {
            if (!cfg::ParseBool(value, in_out_config.vsync)) {
                out_error = "vsync expects boolean: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "debug_input_enabled") {
            if (!cfg::ParseBool(value, in_out_config.debug_input_enabled)) {
                out_error = "debug_input_enabled expects boolean: line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "strict_save_mod_fingerprint") {
            if (!cfg::ParseBool(value, in_out_config.strict_save_mod_fingerprint)) {
                out_error =
                    "strict_save_mod_fingerprint expects boolean: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "mod_root") {
            if (!ParseString(value, in_out_config.mod_root) || in_out_config.mod_root.empty()) {
                out_error = "mod_root expects non-empty string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "save_root") {
            if (!ParseString(value, in_out_config.save_root) || in_out_config.save_root.empty()) {
                out_error = "save_root expects non-empty string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_udp_local_port") {
            if (!ParsePort(value, in_out_config.net_udp_local_port)) {
                out_error = "net_udp_local_port expects integer within [0,65535]: line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_udp_local_host") {
            if (!ParseString(value, in_out_config.net_udp_local_host)) {
                out_error = "net_udp_local_host expects string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_udp_remote_host") {
            if (!ParseString(value, in_out_config.net_udp_remote_host)) {
                out_error = "net_udp_remote_host expects string: line " + std::to_string(line_number);
                return false;
            }
            continue;
        }

        if (key == "net_udp_remote_port") {
            if (!ParsePort(value, in_out_config.net_udp_remote_port)) {
                out_error = "net_udp_remote_port expects integer within [0,65535]: line " +
                    std::to_string(line_number);
                return false;
            }
            continue;
        }

        out_error =
            "Unknown config key: " + key +
            " (line " + std::to_string(line_number) + ")";
        return false;
    }

    if (in_out_config.window_width <= 0 || in_out_config.window_height <= 0) {
        out_error = "Window size must be greater than zero.";
        return false;
    }

    if (in_out_config.net_udp_local_host.empty()) {
        out_error = "net_udp_local_host cannot be empty.";
        return false;
    }

    if (in_out_config.net_udp_remote_host.empty()) {
        out_error = "net_udp_remote_host cannot be empty.";
        return false;
    }

    out_error.clear();
    return true;
}

}  // namespace

bool ConfigLoader::Load(
    const std::filesystem::path& file_path,
    GameConfig& out_config,
    std::string& out_error) {
    std::vector<cfg::KeyValueLine> lines;
    if (!cfg::ParseFile(file_path, lines, out_error)) {
        return false;
    }

    GameConfig candidate = out_config;
    if (!ApplyConfigLines(lines, candidate, out_error)) {
        return false;
    }

    out_config = std::move(candidate);
    return true;
}

bool ConfigLoader::LoadEmbeddedDefaults(GameConfig& out_config, std::string& out_error) {
    std::vector<cfg::KeyValueLine> lines;
    if (!cfg::ParseText(kEmbeddedDefaultConfig, lines, out_error)) {
        return false;
    }

    GameConfig candidate = out_config;
    if (!ApplyConfigLines(lines, candidate, out_error)) {
        return false;
    }

    out_config = std::move(candidate);
    return true;
}

}  // namespace novaria::core
