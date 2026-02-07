#include "net/net_service_runtime.h"

#include <cstdint>
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
    std::string error;

    novaria::net::NetServiceRuntime runtime;
    runtime.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    runtime.ConfigureUdpBackend("127.0.0.1", 0, {.host = "127.0.0.1", .port = 0});
    passed &= Expect(runtime.Initialize(error), "UDP loopback backend init should succeed.");
    passed &= Expect(error.empty(), "UDP loopback backend init should not return error.");
    passed &= Expect(
        runtime.ActiveBackend() == novaria::net::NetBackendKind::UdpLoopback,
        "Runtime should use UDP loopback backend.");
    runtime.RequestConnect();
    for (std::uint64_t tick = 1; tick <= 20; ++tick) {
        runtime.Tick({.tick_index = tick, .fixed_delta_seconds = 1.0 / 60.0});
        if (runtime.SessionState() == novaria::net::NetSessionState::Connected) {
            break;
        }
    }
    passed &= Expect(
        runtime.SessionState() == novaria::net::NetSessionState::Connected,
        "Runtime should connect through UDP handshake.");
    runtime.PublishWorldSnapshot(21, {"payload"});
    runtime.Tick({.tick_index = 22, .fixed_delta_seconds = 1.0 / 60.0});
    const auto payloads = runtime.ConsumeRemoteChunkPayloads();
    passed &= Expect(payloads.size() == 1 && payloads.front() == "payload", "Runtime should loopback payload.");
    runtime.Shutdown();

    novaria::net::NetServiceRuntime invalid_runtime;
    invalid_runtime.ConfigureUdpBackend("not-an-ipv4-host", 0, {.host = "127.0.0.1", .port = 0});
    passed &= Expect(
        !invalid_runtime.Initialize(error),
        "Invalid local bind host should fail runtime initialization.");
    passed &= Expect(
        !error.empty(),
        "Invalid local bind host should provide readable runtime error.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_net_service_runtime_tests\n";
    return 0;
}
