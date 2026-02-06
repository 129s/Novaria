#include "net/net_service_stub.h"

#include <iostream>
#include <limits>
#include <string>

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
    novaria::net::NetServiceStub net_service;
    std::string error;

    passed &= Expect(net_service.Initialize(error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error message.");
    passed &= Expect(net_service.PendingCommandCount() == 0, "Pending command count should start at zero.");
    passed &= Expect(
        net_service.PendingRemoteChunkPayloadCount() == 0,
        "Pending remote payload count should start at zero.");
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Disconnected,
        "Session should start in disconnected state.");
    passed &= Expect(net_service.TotalProcessedCommandCount() == 0, "Processed command count should start at zero.");
    passed &= Expect(net_service.DroppedCommandCount() == 0, "Dropped command count should start at zero.");
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadCount() == 0,
        "Dropped remote payload count should start at zero.");
    passed &= Expect(net_service.ConnectRequestCount() == 0, "Connect request count should start at zero.");
    passed &= Expect(net_service.TimeoutDisconnectCount() == 0, "Timeout disconnect count should start at zero.");
    passed &= Expect(
        net_service.LastPublishedSnapshotTick() == std::numeric_limits<std::uint64_t>::max(),
        "Last snapshot tick should be sentinel before first publish.");

    net_service.RequestConnect();
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connecting,
        "RequestConnect should move session to connecting state.");
    passed &= Expect(net_service.ConnectRequestCount() == 1, "Connect request count should increment.");

    net_service.SubmitLocalCommand({.player_id = 7, .command_type = "move", .payload = "right"});
    net_service.SubmitLocalCommand({.player_id = 8, .command_type = "jump", .payload = ""});
    passed &= Expect(net_service.PendingCommandCount() == 2, "Two commands should be queued.");

    net_service.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connected,
        "Tick should advance connecting session to connected.");
    passed &= Expect(net_service.LastHeartbeatTick() == 1, "Connected tick should set heartbeat baseline.");
    passed &= Expect(net_service.PendingCommandCount() == 0, "Queue should be drained after tick.");
    passed &= Expect(net_service.TotalProcessedCommandCount() == 2, "Processed command count should increase.");

    net_service.PublishWorldSnapshot(42, {"chunk_a", "chunk_b", "chunk_c"});
    passed &= Expect(net_service.LastPublishedSnapshotTick() == 42, "Last snapshot tick should update.");
    passed &= Expect(net_service.LastPublishedDirtyChunkCount() == 3, "Last dirty chunk count should update.");
    passed &= Expect(net_service.SnapshotPublishCount() == 1, "Snapshot publish count should increment.");
    passed &= Expect(
        net_service.LastPublishedEncodedChunks().size() == 3,
        "Published encoded chunk payload count should match.");
    if (net_service.LastPublishedEncodedChunks().size() == 3) {
        passed &= Expect(
            net_service.LastPublishedEncodedChunks()[1] == "chunk_b",
            "Published encoded chunk payload should preserve ordering.");
    }

    net_service.Tick({
        .tick_index = 1 + novaria::net::NetServiceStub::kHeartbeatTimeoutTicks,
        .fixed_delta_seconds = 1.0 / 60.0,
    });
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connected,
        "Session should still be connected at heartbeat timeout boundary.");

    net_service.Tick({
        .tick_index = 1 + novaria::net::NetServiceStub::kHeartbeatTimeoutTicks + 1,
        .fixed_delta_seconds = 1.0 / 60.0,
    });
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Disconnected,
        "Session should disconnect after heartbeat timeout.");
    passed &= Expect(net_service.TimeoutDisconnectCount() == 1, "Heartbeat timeout should increment counter.");

    net_service.RequestConnect();
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connecting,
        "Session should re-enter connecting state after reconnect request.");
    net_service.Tick({.tick_index = 1000, .fixed_delta_seconds = 1.0 / 60.0});
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connected,
        "Reconnect request should recover to connected state.");
    net_service.NotifyHeartbeatReceived(1020);
    net_service.Tick({
        .tick_index = 1020 + novaria::net::NetServiceStub::kHeartbeatTimeoutTicks,
        .fixed_delta_seconds = 1.0 / 60.0,
    });
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connected,
        "Heartbeat update should keep session connected.");
    net_service.RequestDisconnect();
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Disconnected,
        "RequestDisconnect should move session to disconnected state.");

    net_service.Shutdown();
    net_service.SubmitLocalCommand({.player_id = 9, .command_type = "attack", .payload = ""});
    passed &= Expect(
        net_service.PendingCommandCount() == 0,
        "Commands submitted after shutdown should be ignored.");

    passed &= Expect(net_service.Initialize(error), "Reinitialize should succeed.");
    net_service.RequestConnect();
    net_service.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    for (std::size_t index = 0; index < novaria::net::NetServiceStub::kMaxPendingCommands + 8; ++index) {
        net_service.SubmitLocalCommand({.player_id = 1, .command_type = "spam", .payload = ""});
    }
    passed &= Expect(
        net_service.PendingCommandCount() == novaria::net::NetServiceStub::kMaxPendingCommands,
        "Pending commands should be clamped to max capacity.");
    passed &= Expect(net_service.DroppedCommandCount() == 8, "Dropped command count should track overflow.");

    for (std::size_t index = 0;
         index < novaria::net::NetServiceStub::kMaxPendingRemoteChunkPayloads + 5;
         ++index) {
        net_service.EnqueueRemoteChunkPayload("remote_chunk_" + std::to_string(index));
    }
    passed &= Expect(
        net_service.PendingRemoteChunkPayloadCount() ==
            novaria::net::NetServiceStub::kMaxPendingRemoteChunkPayloads,
        "Pending remote payloads should be clamped to max capacity.");
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadCount() == 5,
        "Dropped remote payload count should track overflow.");

    auto remote_payloads = net_service.ConsumeRemoteChunkPayloads();
    passed &= Expect(
        remote_payloads.size() == novaria::net::NetServiceStub::kMaxPendingRemoteChunkPayloads,
        "Remote payload consume count should match clamped capacity.");
    passed &= Expect(
        net_service.ConsumeRemoteChunkPayloads().empty(),
        "Remote payload queue should be empty after consume.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_net_service_stub_tests\n";
    return 0;
}
