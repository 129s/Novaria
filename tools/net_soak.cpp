#include "net/net_service_runtime.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct SoakOptions final {
    std::string role;
    std::string local_host = "0.0.0.0";
    std::uint16_t local_port = 0;
    std::string remote_host = "127.0.0.1";
    std::uint16_t remote_port = 0;
    std::uint64_t ticks = 108000;
    std::uint64_t payload_interval_ticks = 30;
    std::uint64_t allow_timeout_disconnects = 0;
    std::uint64_t inject_pause_tick = 0;
    std::uint64_t inject_pause_ms = 0;
};

bool ParseUInt16(std::string_view text, std::uint16_t& out_value) {
    try {
        const int parsed = std::stoi(std::string(text));
        if (parsed < 0 || parsed > 65535) {
            return false;
        }

        out_value = static_cast<std::uint16_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseUInt64(std::string_view text, std::uint64_t& out_value) {
    try {
        out_value = static_cast<std::uint64_t>(std::stoull(std::string(text)));
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseArguments(
    int argc,
    char** argv,
    SoakOptions& out_options,
    std::string& out_error) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto read_value = [&](const char* key) -> std::string {
            if (index + 1 >= argc) {
                out_error = std::string("Missing value for option: ") + key;
                return {};
            }
            ++index;
            return argv[index];
        };

        if (arg == "--role") {
            out_options.role = read_value("--role");
            if (out_options.role.empty()) {
                return false;
            }
            if (out_options.role != "host" && out_options.role != "client") {
                out_error = "role only supports host/client";
                return false;
            }
            continue;
        }

        if (arg == "--local-host") {
            out_options.local_host = read_value("--local-host");
            if (out_options.local_host.empty()) {
                return false;
            }
            continue;
        }

        if (arg == "--local-port") {
            const std::string value = read_value("--local-port");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt16(value, out_options.local_port)) {
                out_error = "Invalid --local-port value";
                return false;
            }
            continue;
        }

        if (arg == "--remote-host") {
            out_options.remote_host = read_value("--remote-host");
            if (out_options.remote_host.empty()) {
                return false;
            }
            continue;
        }

        if (arg == "--remote-port") {
            const std::string value = read_value("--remote-port");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt16(value, out_options.remote_port)) {
                out_error = "Invalid --remote-port value";
                return false;
            }
            continue;
        }

        if (arg == "--ticks") {
            const std::string value = read_value("--ticks");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt64(value, out_options.ticks)) {
                out_error = "Invalid --ticks value";
                return false;
            }
            continue;
        }

        if (arg == "--payload-interval") {
            const std::string value = read_value("--payload-interval");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt64(value, out_options.payload_interval_ticks)) {
                out_error = "Invalid --payload-interval value";
                return false;
            }
            continue;
        }

        if (arg == "--allow-timeout-disconnects") {
            const std::string value = read_value("--allow-timeout-disconnects");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt64(value, out_options.allow_timeout_disconnects)) {
                out_error = "Invalid --allow-timeout-disconnects value";
                return false;
            }
            continue;
        }

        if (arg == "--inject-pause-tick") {
            const std::string value = read_value("--inject-pause-tick");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt64(value, out_options.inject_pause_tick)) {
                out_error = "Invalid --inject-pause-tick value";
                return false;
            }
            continue;
        }

        if (arg == "--inject-pause-ms") {
            const std::string value = read_value("--inject-pause-ms");
            if (value.empty()) {
                return false;
            }
            if (!ParseUInt64(value, out_options.inject_pause_ms)) {
                out_error = "Invalid --inject-pause-ms value";
                return false;
            }
            continue;
        }

        out_error = "Unknown option: " + arg;
        return false;
    }

    if (out_options.role.empty()) {
        out_error = "role is required (--role host|client)";
        return false;
    }
    if (out_options.remote_port == 0) {
        out_error = "remote_port cannot be zero";
        return false;
    }
    if (out_options.ticks == 0) {
        out_error = "ticks must be > 0";
        return false;
    }
    if (out_options.payload_interval_ticks == 0) {
        out_error = "payload_interval must be > 0";
        return false;
    }

    out_error.clear();
    return true;
}

void PrintUsage() {
    std::cout
        << "Usage:\n"
        << "  novaria_net_soak --role <host|client> "
        << "--local-host <ip> --local-port <port> "
        << "--remote-host <ip> --remote-port <port> "
        << "[--ticks <count>] [--payload-interval <count>] "
        << "[--allow-timeout-disconnects <count>] "
        << "[--inject-pause-tick <tick>] [--inject-pause-ms <ms>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    SoakOptions options{};
    std::string error;
    if (!ParseArguments(argc, argv, options, error)) {
        std::cerr << "[ERROR] " << error << '\n';
        PrintUsage();
        return 1;
    }

    novaria::net::NetServiceRuntime net_runtime;
    net_runtime.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    net_runtime.ConfigureUdpBackend(
        options.local_host,
        options.local_port,
        novaria::net::UdpEndpoint{
            .host = options.remote_host,
            .port = options.remote_port,
        });

    if (!net_runtime.Initialize(error)) {
        std::cerr << "[ERROR] net init failed: " << error << '\n';
        return 1;
    }

    const std::string payload_prefix =
        options.role == "host" ? "soak.host" : "soak.client";
    std::uint64_t sent_payload_count = 0;
    std::uint64_t received_payload_count = 0;
    std::uint64_t disconnected_tick_count = 0;
    std::uint64_t reconnect_request_count = 0;
    bool connected_once = false;
    net_runtime.RequestConnect();

    for (std::uint64_t tick = 0; tick < options.ticks; ++tick) {
        if (options.inject_pause_ms > 0 && tick == options.inject_pause_tick) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(options.inject_pause_ms));
        }

        net_runtime.Tick({.tick_index = tick, .fixed_delta_seconds = 1.0 / 60.0});

        const novaria::net::NetSessionState session_state = net_runtime.SessionState();
        if (session_state == novaria::net::NetSessionState::Connected) {
            connected_once = true;
            if (tick % options.payload_interval_ticks == 0) {
                net_runtime.PublishWorldSnapshot(
                    tick,
                    {payload_prefix + ".tick=" + std::to_string(tick)});
                ++sent_payload_count;
            }
        } else if (session_state == novaria::net::NetSessionState::Disconnected) {
            ++disconnected_tick_count;
            net_runtime.RequestConnect();
            ++reconnect_request_count;
        }

        const std::vector<std::string> payloads = net_runtime.ConsumeRemoteChunkPayloads();
        received_payload_count += payloads.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    const novaria::net::NetDiagnosticsSnapshot diagnostics = net_runtime.DiagnosticsSnapshot();
    std::cout
        << "[INFO] summary: role=" << options.role
        << ", sent_payload_count=" << sent_payload_count
        << ", received_payload_count=" << received_payload_count
        << ", disconnected_tick_count=" << disconnected_tick_count
        << ", reconnect_requests=" << reconnect_request_count
        << ", session_transitions=" << diagnostics.session_transition_count
        << ", timeout_disconnects=" << diagnostics.timeout_disconnect_count
        << ", ignored_senders=" << diagnostics.ignored_unexpected_sender_count
        << '\n';

    net_runtime.Shutdown();

    const bool pass =
        connected_once &&
        sent_payload_count > 0 &&
        received_payload_count > 0 &&
        diagnostics.timeout_disconnect_count <= options.allow_timeout_disconnects;
    if (!pass) {
        std::cerr
            << "[FAIL] soak criteria not satisfied: connected_once=" << connected_once
            << ", sent_payload_count=" << sent_payload_count
            << ", received_payload_count=" << received_payload_count
            << ", timeout_disconnects=" << diagnostics.timeout_disconnect_count
            << ", allow_timeout_disconnects=" << options.allow_timeout_disconnects
            << '\n';
        return 1;
    }

    std::cout << "[PASS] novaria_net_soak\n";
    return 0;
}
