#include "net/net_service_udp_loopback.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool passed = true;
    std::string error;

    novaria::net::NetServiceUdpLoopback net_service;
    passed &= Expect(net_service.Initialize(error), "UDP loopback net service initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error.");
    passed &= Expect(net_service.LocalPort() != 0, "UDP loopback net service should expose local port.");
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Disconnected,
        "Initial state should be disconnected.");

    net_service.RequestConnect();
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connecting,
        "RequestConnect should move state to connecting.");
    for (std::uint64_t tick = 1; tick <= 10; ++tick) {
        net_service.Tick({.tick_index = tick, .fixed_delta_seconds = 1.0 / 60.0});
        if (net_service.SessionState() == novaria::net::NetSessionState::Connected) {
            break;
        }
    }
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connected,
        "Handshake ticks should move connecting state to connected.");

    net_service.SubmitLocalCommand({.player_id = 1, .command_type = "jump", .payload = "{}"});
    net_service.Tick({.tick_index = 2, .fixed_delta_seconds = 1.0 / 60.0});

    const std::vector<std::string> encoded_chunks = {"chunk_payload_1", "chunk_payload_2"};
    net_service.PublishWorldSnapshot(3, encoded_chunks);
    net_service.Tick({.tick_index = 4, .fixed_delta_seconds = 1.0 / 60.0});
    const auto consumed_payloads = net_service.ConsumeRemoteChunkPayloads();
    passed &= Expect(
        consumed_payloads.size() == encoded_chunks.size(),
        "Loopback transport should receive published snapshots.");
    passed &= Expect(
        consumed_payloads == encoded_chunks,
        "Consumed payloads should preserve publish order.");

    net_service.NotifyHeartbeatReceived(5);
    net_service.Tick({
        .tick_index = 5 + novaria::net::NetServiceUdpLoopback::kHeartbeatTimeoutTicks + 1,
        .fixed_delta_seconds = 1.0 / 60.0,
    });
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Disconnected,
        "Service should disconnect after heartbeat timeout.");
    passed &= Expect(
        net_service.DiagnosticsSnapshot().timeout_disconnect_count == 1,
        "Heartbeat timeout should update diagnostics.");

    net_service.Shutdown();
    net_service.RequestConnect();
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Disconnected,
        "Connect request after shutdown should be ignored.");

    novaria::net::NetServiceUdpLoopback host_a;
    novaria::net::NetServiceUdpLoopback host_b;
    passed &= Expect(host_a.Initialize(error), "Host A init should succeed.");
    passed &= Expect(host_b.Initialize(error), "Host B init should succeed.");
    host_a.SetRemoteEndpoint({.host = "127.0.0.1", .port = host_b.LocalPort()});
    host_b.SetRemoteEndpoint({.host = "127.0.0.1", .port = host_a.LocalPort()});

    host_a.RequestConnect();
    host_b.RequestConnect();
    for (std::uint64_t tick = 1; tick <= 20; ++tick) {
        host_a.Tick({.tick_index = tick, .fixed_delta_seconds = 1.0 / 60.0});
        host_b.Tick({.tick_index = tick, .fixed_delta_seconds = 1.0 / 60.0});
        if (host_a.SessionState() == novaria::net::NetSessionState::Connected &&
            host_b.SessionState() == novaria::net::NetSessionState::Connected) {
            break;
        }
    }
    passed &= Expect(
        host_a.SessionState() == novaria::net::NetSessionState::Connected &&
            host_b.SessionState() == novaria::net::NetSessionState::Connected,
        "Both hosts should enter connected state after handshake.");

    host_a.PublishWorldSnapshot(2, {"cross_process_payload"});
    host_a.Tick({.tick_index = 2, .fixed_delta_seconds = 1.0 / 60.0});
    host_b.Tick({.tick_index = 2, .fixed_delta_seconds = 1.0 / 60.0});
    const auto host_b_payloads = host_b.ConsumeRemoteChunkPayloads();
    passed &= Expect(
        host_b_payloads.size() == 1 && host_b_payloads.front() == "cross_process_payload",
        "Host B should receive payload published by Host A.");

    host_b.PublishWorldSnapshot(3, {"cross_process_payload_back"});
    host_b.Tick({.tick_index = 3, .fixed_delta_seconds = 1.0 / 60.0});
    host_a.Tick({.tick_index = 3, .fixed_delta_seconds = 1.0 / 60.0});
    const auto host_a_payloads = host_a.ConsumeRemoteChunkPayloads();
    passed &= Expect(
        host_a_payloads.size() == 1 && host_a_payloads.front() == "cross_process_payload_back",
        "Host A should receive payload published by Host B.");

    host_a.Shutdown();
    host_b.Shutdown();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_net_service_udp_loopback_tests\n";
    return 0;
}
