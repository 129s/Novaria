#include "net/net_service_runtime.h"

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

    runtime.SetBackendPreference(novaria::net::NetBackendPreference::Auto);
    passed &= Expect(runtime.Initialize(error), "Auto backend init should succeed.");
    passed &= Expect(
        runtime.ActiveBackend() != novaria::net::NetBackendKind::None,
        "Auto backend should choose an active backend.");
    runtime.RequestConnect();
    runtime.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
    runtime.Shutdown();

    runtime.SetBackendPreference(novaria::net::NetBackendPreference::Stub);
    passed &= Expect(runtime.Initialize(error), "Stub backend init should succeed.");
    passed &= Expect(
        runtime.ActiveBackend() == novaria::net::NetBackendKind::Stub,
        "Stub preference should select stub backend.");
    runtime.Shutdown();

    runtime.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    runtime.ConfigureUdpBackend(0, {.host = "127.0.0.1", .port = 0});
    const bool udp_init_ok = runtime.Initialize(error);
    if (udp_init_ok) {
        passed &= Expect(
            runtime.ActiveBackend() == novaria::net::NetBackendKind::UdpLoopback,
            "UDP loopback preference should select UDP loopback backend.");
        runtime.RequestConnect();
        runtime.Tick({.tick_index = 1, .fixed_delta_seconds = 1.0 / 60.0});
        runtime.PublishWorldSnapshot(2, {"payload"});
        runtime.Tick({.tick_index = 3, .fixed_delta_seconds = 1.0 / 60.0});
        runtime.Shutdown();
    } else {
        passed &= Expect(
            !error.empty(),
            "UDP loopback backend init failure should return readable error.");
    }

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_net_service_runtime_tests\n";
    return 0;
}
