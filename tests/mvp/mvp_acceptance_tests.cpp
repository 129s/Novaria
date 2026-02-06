#include "mod/mod_loader.h"
#include "net/net_service_runtime.h"
#include "save/save_repository.h"
#include "script/script_host.h"
#include "sim/command_schema.h"
#include "sim/simulation_kernel.h"
#include "world/world_service_basic.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

std::filesystem::path BuildTempDirectory(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

bool WriteTextFile(const std::filesystem::path& file_path, const std::string& content) {
    std::ofstream file(file_path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    return true;
}

class AcceptanceScriptHost final : public novaria::script::IScriptHost {
public:
    bool Initialize(std::string& out_error) override {
        out_error.clear();
        return true;
    }

    void Shutdown() override {}

    void Tick(const novaria::sim::TickContext& tick_context) override {
        (void)tick_context;
    }

    void DispatchEvent(const novaria::script::ScriptEvent& event_data) override {
        received_events_.push_back(event_data);
    }

    novaria::script::ScriptRuntimeDescriptor RuntimeDescriptor() const override {
        return novaria::script::ScriptRuntimeDescriptor{
            .backend_name = "acceptance_fake",
            .api_version = novaria::script::kScriptApiVersion,
            .sandbox_enabled = false,
        };
    }

private:
    std::vector<novaria::script::ScriptEvent> received_events_;
};

void SubmitPlayableLoopCommands(
    novaria::sim::SimulationKernel& kernel,
    std::uint32_t player_id) {
    kernel.SubmitLocalCommand({
        .player_id = player_id,
        .command_type = std::string(novaria::sim::command::kGameplayCollectResource),
        .payload = novaria::sim::command::BuildCollectResourcePayload(
            novaria::sim::command::kResourceWood,
            20),
    });
    kernel.SubmitLocalCommand({
        .player_id = player_id,
        .command_type = std::string(novaria::sim::command::kGameplayCollectResource),
        .payload = novaria::sim::command::BuildCollectResourcePayload(
            novaria::sim::command::kResourceStone,
            20),
    });
    kernel.SubmitLocalCommand({
        .player_id = player_id,
        .command_type = std::string(novaria::sim::command::kGameplayBuildWorkbench),
        .payload = "",
    });
    kernel.SubmitLocalCommand({
        .player_id = player_id,
        .command_type = std::string(novaria::sim::command::kGameplayCraftSword),
        .payload = "",
    });
    for (int index = 0; index < 3; ++index) {
        kernel.SubmitLocalCommand({
            .player_id = player_id,
            .command_type = std::string(novaria::sim::command::kGameplayAttackEnemy),
            .payload = "",
        });
    }
    for (int index = 0; index < 6; ++index) {
        kernel.SubmitLocalCommand({
            .player_id = player_id,
            .command_type = std::string(novaria::sim::command::kGameplayAttackBoss),
            .payload = "",
        });
    }
}

bool TestPlayableLoopAndSaveReload() {
    bool passed = true;
    const std::filesystem::path save_root = BuildTempDirectory("novaria_mvp_acceptance_save");
    std::error_code ec;
    std::filesystem::remove_all(save_root, ec);

    novaria::world::WorldServiceBasic world;
    novaria::net::NetServiceRuntime net;
    net.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    net.ConfigureUdpBackend(0, {.host = "127.0.0.1", .port = 0});
    AcceptanceScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Simulation kernel should initialize.");
    SubmitPlayableLoopCommands(kernel, 7);
    kernel.Update(1.0 / 60.0);

    const novaria::sim::GameplayProgressSnapshot progress = kernel.GameplayProgress();
    passed &= Expect(progress.playable_loop_complete, "Gameplay loop should reach completion.");

    novaria::save::FileSaveRepository repository;
    passed &= Expect(repository.Initialize(save_root, error), "Save repository should initialize.");
    const novaria::net::NetDiagnosticsSnapshot diagnostics = net.DiagnosticsSnapshot();
    const novaria::save::WorldSaveState save_state{
        .tick_index = kernel.CurrentTick(),
        .local_player_id = 7,
        .mod_manifest_fingerprint = "mvp_acceptance",
        .gameplay_wood_collected = progress.wood_collected,
        .gameplay_stone_collected = progress.stone_collected,
        .gameplay_workbench_built = progress.workbench_built,
        .gameplay_sword_crafted = progress.sword_crafted,
        .gameplay_enemy_kill_count = progress.enemy_kill_count,
        .gameplay_boss_health = progress.boss_health,
        .gameplay_boss_defeated = progress.boss_defeated,
        .gameplay_loop_complete = progress.playable_loop_complete,
        .has_gameplay_snapshot = true,
        .debug_net_session_transitions = diagnostics.session_transition_count,
        .debug_net_timeout_disconnects = diagnostics.timeout_disconnect_count,
        .debug_net_manual_disconnects = diagnostics.manual_disconnect_count,
        .debug_net_last_heartbeat_tick = diagnostics.last_heartbeat_tick,
        .debug_net_dropped_commands = diagnostics.dropped_command_count,
        .debug_net_dropped_remote_payloads = diagnostics.dropped_remote_chunk_payload_count,
        .debug_net_last_transition_reason = diagnostics.last_session_transition_reason,
    };

    passed &= Expect(repository.SaveWorldState(save_state, error), "Save should succeed.");

    novaria::save::WorldSaveState loaded_state{};
    passed &= Expect(repository.LoadWorldState(loaded_state, error), "Load should succeed.");
    passed &= Expect(
        loaded_state.has_gameplay_snapshot && loaded_state.gameplay_loop_complete &&
            loaded_state.gameplay_boss_defeated,
        "Loaded save should retain gameplay completion progress.");

    repository.Shutdown();
    kernel.Shutdown();
    std::filesystem::remove_all(save_root, ec);
    return passed;
}

bool TestFourPlayerThirtyMinuteSimulationStability() {
    bool passed = true;

    novaria::world::WorldServiceBasic world;
    novaria::net::NetServiceRuntime net;
    net.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    net.ConfigureUdpBackend(0, {.host = "127.0.0.1", .port = 0});
    AcceptanceScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Simulation kernel should initialize.");
    kernel.Update(1.0 / 60.0);

    constexpr std::uint64_t kThirtyMinutesTicks = 60 * 60 * 30;
    constexpr std::uint32_t kPlayerCount = 4;
    for (std::uint64_t tick = 0; tick < kThirtyMinutesTicks; ++tick) {
        for (std::uint32_t player_id = 1; player_id <= kPlayerCount; ++player_id) {
            kernel.SubmitLocalCommand({
                .player_id = player_id,
                .command_type = std::string(novaria::sim::command::kJump),
                .payload = "",
            });
        }

        if (tick % 30 == 0) {
            net.NotifyHeartbeatReceived(kernel.CurrentTick());
        }

        kernel.Update(1.0 / 60.0);
    }

    const novaria::net::NetDiagnosticsSnapshot diagnostics = net.DiagnosticsSnapshot();
    passed &= Expect(
        diagnostics.session_state == novaria::net::NetSessionState::Connected,
        "Thirty-minute run should keep net session connected.");
    passed &= Expect(
        diagnostics.timeout_disconnect_count == 0,
        "Thirty-minute run should not produce heartbeat timeout disconnects.");
    passed &= Expect(
        kernel.DroppedLocalCommandCount() == 0,
        "Thirty-minute run should not overflow local command queue.");

    kernel.Shutdown();
    return passed;
}

bool TestModContentConsistencyFingerprint() {
    bool passed = true;
    const std::filesystem::path mod_root = BuildTempDirectory("novaria_mvp_acceptance_mod");
    std::error_code ec;
    std::filesystem::remove_all(mod_root, ec);

    std::filesystem::create_directories(mod_root / "core" / "content", ec);
    std::filesystem::create_directories(mod_root / "expansion" / "content", ec);
    passed &= Expect(!ec, "Mod test directory create should succeed.");

    passed &= Expect(
        WriteTextFile(
            mod_root / "core" / "mod.toml",
            "name = \"core\"\n"
            "version = \"1.0.0\"\n"
            "dependencies = []\n"),
        "Core mod manifest write should succeed.");
    passed &= Expect(
        WriteTextFile(
            mod_root / "core" / "content" / "items.csv",
            "iron_sword,weapon.damage+7\n"),
        "Core mod items write should succeed.");
    passed &= Expect(
        WriteTextFile(
            mod_root / "expansion" / "mod.toml",
            "name = \"expansion\"\n"
            "version = \"1.0.0\"\n"
            "dependencies = [\"core\"]\n"),
        "Expansion mod manifest write should succeed.");
    passed &= Expect(
        WriteTextFile(
            mod_root / "expansion" / "content" / "npcs.csv",
            "mini_boss,180,boss.charge\n"),
        "Expansion mod npc write should succeed.");

    novaria::mod::ModLoader loader;
    std::string error;
    passed &= Expect(loader.Initialize(mod_root, error), "Mod loader should initialize.");

    std::vector<novaria::mod::ModManifest> manifests;
    passed &= Expect(loader.LoadAll(manifests, error), "Mod loader should load manifests.");
    const std::string fingerprint_a = novaria::mod::ModLoader::BuildManifestFingerprint(manifests);
    passed &= Expect(!fingerprint_a.empty(), "Fingerprint should not be empty.");

    passed &= Expect(
        WriteTextFile(
            mod_root / "expansion" / "content" / "npcs.csv",
            "mini_boss,180,boss.frenzy\n"),
        "Expansion mod npc rewrite should succeed.");
    manifests.clear();
    passed &= Expect(loader.LoadAll(manifests, error), "Mod loader should reload manifests.");
    const std::string fingerprint_b = novaria::mod::ModLoader::BuildManifestFingerprint(manifests);
    passed &= Expect(
        fingerprint_a != fingerprint_b,
        "Fingerprint should change when mod behavior content changes.");

    loader.Shutdown();
    std::filesystem::remove_all(mod_root, ec);
    return passed;
}

bool TestTickP95PerformanceBudget() {
    bool passed = true;

    novaria::world::WorldServiceBasic world;
    novaria::net::NetServiceRuntime net;
    net.SetBackendPreference(novaria::net::NetBackendPreference::UdpLoopback);
    net.ConfigureUdpBackend(0, {.host = "127.0.0.1", .port = 0});
    AcceptanceScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Simulation kernel should initialize.");
    kernel.Update(1.0 / 60.0);

    constexpr int kMeasuredTicks = 1200;
    std::vector<double> tick_durations_ms;
    tick_durations_ms.reserve(kMeasuredTicks);
    for (int tick = 0; tick < kMeasuredTicks; ++tick) {
        net.NotifyHeartbeatReceived(kernel.CurrentTick());
        const auto start_time = std::chrono::steady_clock::now();
        kernel.Update(1.0 / 60.0);
        const auto end_time = std::chrono::steady_clock::now();

        const auto elapsed_microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        tick_durations_ms.push_back(static_cast<double>(elapsed_microseconds) / 1000.0);
    }

    std::sort(tick_durations_ms.begin(), tick_durations_ms.end());
    const std::size_t p95_index = (tick_durations_ms.size() * 95) / 100;
    const std::size_t clamped_index =
        p95_index >= tick_durations_ms.size() ? tick_durations_ms.size() - 1 : p95_index;
    const double p95_ms = tick_durations_ms[clamped_index];
    passed &= Expect(p95_ms <= 16.6, "Simulation Tick P95 should stay under 16.6ms.");

    kernel.Shutdown();
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestPlayableLoopAndSaveReload();
    passed &= TestFourPlayerThirtyMinuteSimulationStability();
    passed &= TestModContentConsistencyFingerprint();
    passed &= TestTickP95PerformanceBudget();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_mvp_acceptance_tests\n";
    return 0;
}
