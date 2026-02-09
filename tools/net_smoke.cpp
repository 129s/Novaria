#include "runtime/net_service_factory.h"
#include "world/snapshot_codec.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct SmokeOptions final {
    std::string role;
    std::string local_host = "0.0.0.0";
    std::uint16_t local_port = 0;
    std::string remote_host = "127.0.0.1";
    std::uint16_t remote_port = 0;
    std::uint64_t ticks = 900;
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
        const unsigned long long parsed = std::stoull(std::string(text));
        out_value = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseArguments(int argc, char** argv, SmokeOptions& out_options, std::string& out_error) {
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

    out_error.clear();
    return true;
}

void PrintUsage() {
    std::cout
        << "Usage:\n"
        << "  novaria_net_smoke --role <host|client> "
        << "--local-host <ip> --local-port <port> "
        << "--remote-host <ip> --remote-port <port> [--ticks <count>]\n";
}

bool TryBuildProbeChunkPayload(
    std::uint64_t tick_index,
    bool is_host,
    novaria::wire::ByteBuffer& out_payload,
    std::string& out_error) {
    novaria::world::ChunkSnapshot snapshot{};
    snapshot.chunk_coord = novaria::world::ChunkCoord{.x = is_host ? 1 : -1, .y = 0};
    snapshot.tiles = {static_cast<std::uint16_t>(tick_index & 0xFFFFu)};
    return novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(
        snapshot,
        out_payload,
        out_error);
}

}  // namespace

int main(int argc, char** argv) {
    SmokeOptions options{};
    std::string error;
    if (!ParseArguments(argc, argv, options, error)) {
        std::cerr << "[ERROR] " << error << '\n';
        PrintUsage();
        return 1;
    }

    auto net_runtime = novaria::runtime::CreateNetService(novaria::runtime::NetServiceConfig{
        .local_host = options.local_host,
        .local_port = options.local_port,
        .remote_endpoint = novaria::net::UdpEndpoint{
            .host = options.remote_host,
            .port = options.remote_port,
        },
    });
    if (!net_runtime->Initialize(error)) {
        std::cerr << "[ERROR] net init failed: " << error << '\n';
        return 1;
    }

    const bool is_host = options.role == "host";
    bool connected_once = false;
    bool payload_sent = false;
    bool payload_received = false;
    net_runtime->RequestConnect();

    for (std::uint64_t tick = 0; tick < options.ticks; ++tick) {
        net_runtime->Tick({.tick_index = tick, .fixed_delta_seconds = 1.0 / 60.0});

        if (net_runtime->SessionState() == novaria::net::NetSessionState::Connected) {
            connected_once = true;
            if (!payload_sent) {
                novaria::wire::ByteBuffer payload;
                if (!TryBuildProbeChunkPayload(tick, is_host, payload, error)) {
                    std::cerr << "[ERROR] build payload failed: " << error << '\n';
                    net_runtime->Shutdown();
                    return 1;
                }

                std::vector<novaria::wire::ByteBuffer> payloads;
                payloads.push_back(std::move(payload));
                net_runtime->PublishWorldSnapshot(tick, payloads);
                payload_sent = true;
            }
        }

        const std::vector<novaria::wire::ByteBuffer> payloads =
            net_runtime->ConsumeRemoteChunkPayloads();
        if (!payloads.empty()) {
            payload_received = true;
            std::cout << "[INFO] received payload count=" << payloads.size() << '\n';
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    const novaria::net::NetDiagnosticsSnapshot diagnostics = net_runtime->DiagnosticsSnapshot();
    std::cout
        << "[INFO] diagnostics: state=" << static_cast<int>(diagnostics.session_state)
        << ", transitions=" << diagnostics.session_transition_count
        << ", connect_requests=" << diagnostics.connect_request_count
        << ", connect_probes=" << diagnostics.connect_probe_send_count
        << ", timeout_disconnects=" << diagnostics.timeout_disconnect_count
        << ", ignored_senders=" << diagnostics.ignored_unexpected_sender_count
        << '\n';

    net_runtime->Shutdown();
    if (!connected_once || !payload_sent || !payload_received) {
        std::cerr
            << "[FAIL] smoke probe incomplete: connected_once=" << connected_once
            << ", payload_sent=" << payload_sent
            << ", payload_received=" << payload_received
            << '\n';
        return 1;
    }

    std::cout << "[PASS] novaria_net_smoke\n";
    return 0;
}
