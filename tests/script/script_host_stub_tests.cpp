#include "script/script_host_stub.h"

#include <iostream>
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
    novaria::script::ScriptHostStub script_host;
    std::string error;

    passed &= Expect(script_host.Initialize(error), "Initialize should succeed.");
    passed &= Expect(error.empty(), "Initialize should not return error message.");
    passed &= Expect(script_host.PendingEventCount() == 0, "Pending event count should start at zero.");
    passed &= Expect(script_host.TotalProcessedEventCount() == 0, "Processed event count should start at zero.");

    script_host.DispatchEvent({.event_name = "on_spawn", .payload = "{player_id:1}"});
    script_host.DispatchEvent({.event_name = "on_damage", .payload = "{value:10}"});
    passed &= Expect(script_host.PendingEventCount() == 2, "Two events should be queued.");

    script_host.Tick({.tick_index = 5, .fixed_delta_seconds = 1.0 / 60.0});
    passed &= Expect(script_host.PendingEventCount() == 0, "Queue should be drained after tick.");
    passed &= Expect(script_host.TotalProcessedEventCount() == 2, "Processed event count should increase.");

    script_host.Shutdown();
    script_host.DispatchEvent({.event_name = "on_shutdown", .payload = ""});
    passed &= Expect(
        script_host.PendingEventCount() == 0,
        "Events dispatched after shutdown should be ignored.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_script_host_stub_tests\n";
    return 0;
}
