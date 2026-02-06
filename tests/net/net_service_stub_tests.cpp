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
    passed &= Expect(net_service.SessionTransitionCount() == 0, "Session transition count should start at zero.");
    passed &= Expect(
        net_service.ConnectedTransitionCount() == 0,
        "Connected transition count should start at zero.");
    passed &= Expect(net_service.ManualDisconnectCount() == 0, "Manual disconnect count should start at zero.");
    passed &= Expect(net_service.IgnoredHeartbeatCount() == 0, "Ignored heartbeat count should start at zero.");
    passed &= Expect(
        net_service.DroppedCommandDisconnectedCount() == 0,
        "Dropped disconnected command count should start at zero.");
    passed &= Expect(
        net_service.DroppedCommandQueueFullCount() == 0,
        "Dropped queue-full command count should start at zero.");
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadDisconnectedCount() == 0,
        "Dropped disconnected remote payload count should start at zero.");
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadQueueFullCount() == 0,
        "Dropped queue-full remote payload count should start at zero.");
    passed &= Expect(
        net_service.LastPublishedSnapshotTick() == std::numeric_limits<std::uint64_t>::max(),
        "Last snapshot tick should be sentinel before first publish.");
    {
        const novaria::net::NetDiagnosticsSnapshot snapshot = net_service.DiagnosticsSnapshot();
        passed &= Expect(
            snapshot.session_state == novaria::net::NetSessionState::Disconnected,
            "Diagnostics snapshot should expose disconnected initial session state.");
        passed &= Expect(
            snapshot.last_session_transition_reason == "initialize",
            "Diagnostics snapshot should expose initial transition reason.");
        passed &= Expect(
            snapshot.last_heartbeat_tick == std::numeric_limits<std::uint64_t>::max(),
            "Diagnostics snapshot should expose invalid last heartbeat tick before connection.");
        passed &= Expect(
            snapshot.connect_request_count == 0 &&
                snapshot.timeout_disconnect_count == 0 &&
                snapshot.dropped_command_count == 0 &&
                snapshot.dropped_remote_chunk_payload_count == 0,
            "Diagnostics snapshot counters should match initial zero state.");
    }

    net_service.NotifyHeartbeatReceived(0);
    net_service.SubmitLocalCommand({.player_id = 1, .command_type = "offline", .payload = ""});
    net_service.EnqueueRemoteChunkPayload("offline_payload");
    passed &= Expect(
        net_service.PendingCommandCount() == 0,
        "Commands should be rejected while session is disconnected.");
    passed &= Expect(
        net_service.PendingRemoteChunkPayloadCount() == 0,
        "Remote payloads should be rejected while session is disconnected.");
    passed &= Expect(
        net_service.DroppedCommandCount() == 1,
        "Disconnected command submit should increase dropped command count.");
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadCount() == 1,
        "Disconnected payload enqueue should increase dropped remote payload count.");
    passed &= Expect(net_service.IgnoredHeartbeatCount() == 1, "Disconnected heartbeat should be counted as ignored.");
    passed &= Expect(
        net_service.DroppedCommandDisconnectedCount() == 1,
        "Disconnected command drop reason counter should increase.");
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadDisconnectedCount() == 1,
        "Disconnected remote payload drop reason counter should increase.");

    net_service.RequestConnect();
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connecting,
        "RequestConnect should move session to connecting state.");
    passed &= Expect(net_service.ConnectRequestCount() == 1, "Connect request count should increment.");
    passed &= Expect(net_service.SessionTransitionCount() == 1, "Session transition count should track connect request.");
    passed &= Expect(
        net_service.DiagnosticsSnapshot().last_session_transition_reason == "request_connect",
        "Diagnostics snapshot should record connect request transition reason.");

    net_service.SubmitLocalCommand({.player_id = 7, .command_type = "move", .payload = "right"});
    net_service.SubmitLocalCommand({.player_id = 8, .command_type = "jump", .payload = ""});
    passed &= Expect(net_service.PendingCommandCount() == 2, "Two commands should be queued.");

    net_service.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    passed &= Expect(
        net_service.SessionState() == novaria::net::NetSessionState::Connected,
        "Tick should advance connecting session to connected.");
    passed &= Expect(net_service.LastHeartbeatTick() == 1, "Connected tick should set heartbeat baseline.");
    passed &= Expect(
        net_service.ConnectedTransitionCount() == 1,
        "Connected transition count should increment after connect completion.");
    passed &= Expect(
        net_service.DiagnosticsSnapshot().last_session_transition_reason == "tick_connect_complete",
        "Diagnostics snapshot should record connect completion transition reason.");
    passed &= Expect(
        net_service.DiagnosticsSnapshot().last_heartbeat_tick == 1,
        "Diagnostics snapshot should expose last heartbeat tick after connect completion.");
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
    passed &= Expect(
        net_service.DiagnosticsSnapshot().last_session_transition_reason == "heartbeat_timeout",
        "Diagnostics snapshot should record timeout transition reason.");
    passed &= Expect(
        net_service.SessionTransitionCount() == 3,
        "Session transition count should include connect and timeout transitions.");

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
    passed &= Expect(net_service.ManualDisconnectCount() == 1, "Manual disconnect count should increment.");
    passed &= Expect(
        net_service.DiagnosticsSnapshot().last_session_transition_reason == "request_disconnect",
        "Diagnostics snapshot should record manual disconnect transition reason.");
    passed &= Expect(
        net_service.SessionTransitionCount() == 6,
        "Session transition count should include reconnect and manual disconnect transitions.");
    net_service.EnqueueRemoteChunkPayload("after_disconnect_payload");
    passed &= Expect(
        net_service.PendingRemoteChunkPayloadCount() == 0,
        "Remote payload should still be rejected after explicit disconnect.");

    net_service.Shutdown();
    net_service.SubmitLocalCommand({.player_id = 9, .command_type = "attack", .payload = ""});
    passed &= Expect(
        net_service.PendingCommandCount() == 0,
        "Commands submitted after shutdown should be ignored.");

    passed &= Expect(net_service.Initialize(error), "Reinitialize should succeed.");
    passed &= Expect(net_service.SessionTransitionCount() == 0, "Reinitialize should reset transition count.");
    passed &= Expect(net_service.DroppedCommandCount() == 0, "Reinitialize should reset dropped command count.");
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadCount() == 0,
        "Reinitialize should reset dropped remote payload count.");
    net_service.RequestConnect();
    net_service.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    for (std::size_t index = 0; index < novaria::net::NetServiceStub::kMaxPendingCommands + 8; ++index) {
        net_service.SubmitLocalCommand({.player_id = 1, .command_type = "spam", .payload = ""});
    }
    passed &= Expect(
        net_service.PendingCommandCount() == novaria::net::NetServiceStub::kMaxPendingCommands,
        "Pending commands should be clamped to max capacity.");
    passed &= Expect(net_service.DroppedCommandCount() == 8, "Dropped command count should track overflow.");
    passed &= Expect(
        net_service.DroppedCommandQueueFullCount() == 8,
        "Queue-full command drop reason counter should track overflow.");

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
    passed &= Expect(
        net_service.DroppedRemoteChunkPayloadQueueFullCount() == 5,
        "Queue-full remote payload drop reason counter should track overflow.");
    {
        const novaria::net::NetDiagnosticsSnapshot snapshot = net_service.DiagnosticsSnapshot();
        passed &= Expect(
            snapshot.session_state == novaria::net::NetSessionState::Connected,
            "Diagnostics snapshot should expose connected session state.");
        passed &= Expect(
            snapshot.dropped_command_count == net_service.DroppedCommandCount() &&
                snapshot.dropped_remote_chunk_payload_count ==
                    net_service.DroppedRemoteChunkPayloadCount(),
            "Diagnostics snapshot drop counters should mirror service counters.");
    }

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
