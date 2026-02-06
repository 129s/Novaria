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
    passed &= Expect(net_service.TotalProcessedCommandCount() == 0, "Processed command count should start at zero.");
    passed &= Expect(
        net_service.LastPublishedSnapshotTick() == std::numeric_limits<std::uint64_t>::max(),
        "Last snapshot tick should be sentinel before first publish.");

    net_service.SubmitLocalCommand({.player_id = 7, .command_type = "move", .payload = "right"});
    net_service.SubmitLocalCommand({.player_id = 8, .command_type = "jump", .payload = ""});
    passed &= Expect(net_service.PendingCommandCount() == 2, "Two commands should be queued.");

    net_service.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    passed &= Expect(net_service.PendingCommandCount() == 0, "Queue should be drained after tick.");
    passed &= Expect(net_service.TotalProcessedCommandCount() == 2, "Processed command count should increase.");

    net_service.PublishWorldSnapshot(42, 3);
    passed &= Expect(net_service.LastPublishedSnapshotTick() == 42, "Last snapshot tick should update.");
    passed &= Expect(net_service.LastPublishedDirtyChunkCount() == 3, "Last dirty chunk count should update.");
    passed &= Expect(net_service.SnapshotPublishCount() == 1, "Snapshot publish count should increment.");

    net_service.Shutdown();
    net_service.SubmitLocalCommand({.player_id = 9, .command_type = "attack", .payload = ""});
    passed &= Expect(
        net_service.PendingCommandCount() == 0,
        "Commands submitted after shutdown should be ignored.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_net_service_stub_tests\n";
    return 0;
}
