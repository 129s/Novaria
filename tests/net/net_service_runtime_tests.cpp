#include "runtime/net_service_factory.h"
#include "sim/command_schema.h"

#include <cstdint>
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

    auto runtime = novaria::runtime::CreateNetService(novaria::runtime::NetServiceConfig{
        .local_host = "127.0.0.1",
        .local_port = 0,
        .remote_endpoint = {.host = "127.0.0.1", .port = 0},
    });
    passed &= Expect(runtime->Initialize(error), "UDP peer backend init should succeed.");
    passed &= Expect(error.empty(), "UDP peer backend init should not return error.");
    runtime->RequestConnect();
    for (std::uint64_t tick = 1; tick <= 20; ++tick) {
        runtime->Tick({.tick_index = tick, .fixed_delta_seconds = 1.0 / 60.0});
        if (runtime->SessionState() == novaria::net::NetSessionState::Connected) {
            break;
        }
    }
    passed &= Expect(
        runtime->SessionState() == novaria::net::NetSessionState::Connected,
        "Runtime should connect through UDP handshake.");
    runtime->SubmitLocalCommand({
        .player_id = 9,
        .command_id = novaria::sim::command::kJump,
        .payload = {},
    });
    runtime->Tick({.tick_index = 21, .fixed_delta_seconds = 1.0 / 60.0});
    const auto commands = runtime->ConsumeRemoteCommands();
    passed &= Expect(
        commands.size() == 1 &&
            commands.front().player_id == 9 &&
            commands.front().command_id == novaria::sim::command::kJump &&
            commands.front().payload.empty(),
        "Runtime should expose peer command queue.");

    runtime->PublishWorldSnapshot(21, {novaria::wire::ByteBuffer{1, 2, 3}});
    runtime->Tick({.tick_index = 22, .fixed_delta_seconds = 1.0 / 60.0});
    const auto payloads = runtime->ConsumeRemoteChunkPayloads();
    passed &= Expect(
        payloads.size() == 1 && payloads.front() == novaria::wire::ByteBuffer{1, 2, 3},
        "Runtime should return peer payload.");
    runtime->Shutdown();

    auto invalid_runtime = novaria::runtime::CreateNetService(novaria::runtime::NetServiceConfig{
        .local_host = "not-an-ipv4-host",
        .local_port = 0,
        .remote_endpoint = {.host = "127.0.0.1", .port = 0},
    });
    passed &= Expect(
        !invalid_runtime->Initialize(error),
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
