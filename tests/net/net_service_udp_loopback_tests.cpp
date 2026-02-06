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
    net_service.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connected,
        "Tick should move connecting state to connected.");

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

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_net_service_udp_loopback_tests\n";
    return 0;
}
