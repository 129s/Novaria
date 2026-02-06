#include "sim/simulation_kernel.h"
#include "sim/command_schema.h"
#include "save/save_repository.h"
#include "net/net_service_udp_loopback.h"
#include "world/snapshot_codec.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

struct SessionStateChangedPayload {
    std::string state;
    std::uint64_t tick_index = 0;
    std::string reason;
};

struct GameplayProgressPayload {
    std::string milestone;
    std::uint64_t tick_index = 0;
};

bool TryParseSessionStateChangedPayload(
    std::string_view payload,
    SessionStateChangedPayload& out_payload) {
    constexpr std::string_view kStatePrefix = "state=";
    constexpr std::string_view kTickPrefix = "tick=";
    constexpr std::string_view kReasonPrefix = "reason=";

    const std::size_t first_separator = payload.find(';');
    if (first_separator == std::string_view::npos) {
        return false;
    }

    const std::size_t second_separator = payload.find(';', first_separator + 1);
    if (second_separator == std::string_view::npos) {
        return false;
    }

    if (payload.find(';', second_separator + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view state_token = payload.substr(0, first_separator);
    const std::string_view tick_token =
        payload.substr(first_separator + 1, second_separator - first_separator - 1);
    const std::string_view reason_token = payload.substr(second_separator + 1);
    if (state_token.rfind(kStatePrefix, 0) != 0 || tick_token.rfind(kTickPrefix, 0) != 0 ||
        reason_token.rfind(kReasonPrefix, 0) != 0) {
        return false;
    }

    const std::string_view state_value = state_token.substr(kStatePrefix.size());
    const std::string_view tick_value = tick_token.substr(kTickPrefix.size());
    const std::string_view reason_value = reason_token.substr(kReasonPrefix.size());
    if (state_value.empty() || tick_value.empty()) {
        return false;
    }

    std::uint64_t parsed_tick_index = 0;
    const std::from_chars_result parse_result =
        std::from_chars(
            tick_value.data(),
            tick_value.data() + tick_value.size(),
            parsed_tick_index);
    if (parse_result.ec != std::errc() ||
        parse_result.ptr != tick_value.data() + tick_value.size()) {
        return false;
    }

    out_payload.state.assign(state_value);
    out_payload.tick_index = parsed_tick_index;
    out_payload.reason.assign(reason_value);
    return true;
}

bool TryParseGameplayProgressPayload(
    std::string_view payload,
    GameplayProgressPayload& out_payload) {
    constexpr std::string_view kMilestonePrefix = "milestone=";
    constexpr std::string_view kTickPrefix = "tick=";

    const std::size_t separator = payload.find(';');
    if (separator == std::string_view::npos) {
        return false;
    }
    if (payload.find(';', separator + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view milestone_token = payload.substr(0, separator);
    const std::string_view tick_token = payload.substr(separator + 1);
    if (milestone_token.rfind(kMilestonePrefix, 0) != 0 ||
        tick_token.rfind(kTickPrefix, 0) != 0) {
        return false;
    }

    const std::string_view milestone_value = milestone_token.substr(kMilestonePrefix.size());
    const std::string_view tick_value = tick_token.substr(kTickPrefix.size());
    if (milestone_value.empty() || tick_value.empty()) {
        return false;
    }

    std::uint64_t parsed_tick_index = 0;
    const std::from_chars_result parse_result =
        std::from_chars(
            tick_value.data(),
            tick_value.data() + tick_value.size(),
            parsed_tick_index);
    if (parse_result.ec != std::errc() ||
        parse_result.ptr != tick_value.data() + tick_value.size()) {
        return false;
    }

    out_payload.milestone.assign(milestone_value);
    out_payload.tick_index = parsed_tick_index;
    return true;
}

std::filesystem::path BuildSimulationKernelSaveTestDirectory() {
    return std::filesystem::temp_directory_path() / "novaria_sim_kernel_save_e2e_test";
}

class FakeWorldService final : public novaria::world::IWorldService {
public:
    bool initialize_success = true;
    bool initialize_called = false;
    bool shutdown_called = false;
    int tick_count = 0;
    std::vector<std::vector<novaria::world::ChunkCoord>> dirty_batches;
    std::vector<novaria::world::ChunkSnapshot> available_snapshots;
    std::vector<novaria::world::ChunkSnapshot> applied_snapshots;
    std::vector<novaria::world::ChunkCoord> loaded_chunks;
    std::vector<novaria::world::ChunkCoord> unloaded_chunks;
    std::vector<novaria::world::TileMutation> applied_tile_mutations;
    std::size_t dirty_batch_cursor = 0;

    bool Initialize(std::string& out_error) override {
        initialize_called = true;
        if (!initialize_success) {
            out_error = "fake world init failed";
            return false;
        }
        out_error.clear();
        return true;
    }

    void Shutdown() override {
        shutdown_called = true;
    }

    void Tick(const novaria::sim::TickContext& tick_context) override {
        (void)tick_context;
        ++tick_count;
    }

    void LoadChunk(const novaria::world::ChunkCoord& chunk_coord) override {
        loaded_chunks.push_back(chunk_coord);
    }

    void UnloadChunk(const novaria::world::ChunkCoord& chunk_coord) override {
        unloaded_chunks.push_back(chunk_coord);
    }

    bool ApplyTileMutation(const novaria::world::TileMutation& mutation, std::string& out_error) override {
        applied_tile_mutations.push_back(mutation);
        out_error.clear();
        return true;
    }

    bool BuildChunkSnapshot(
        const novaria::world::ChunkCoord& chunk_coord,
        novaria::world::ChunkSnapshot& out_snapshot,
        std::string& out_error) const override {
        for (const auto& snapshot : available_snapshots) {
            if (snapshot.chunk_coord.x == chunk_coord.x && snapshot.chunk_coord.y == chunk_coord.y) {
                out_snapshot = snapshot;
                out_error.clear();
                return true;
            }
        }

        out_error = "snapshot not found";
        return false;
    }

    bool ApplyChunkSnapshot(const novaria::world::ChunkSnapshot& snapshot, std::string& out_error) override {
        applied_snapshots.push_back(snapshot);
        out_error.clear();
        return true;
    }

    std::vector<novaria::world::ChunkCoord> ConsumeDirtyChunks() override {
        if (dirty_batch_cursor >= dirty_batches.size()) {
            return {};
        }

        return dirty_batches[dirty_batch_cursor++];
    }
};

class FakeNetService final : public novaria::net::INetService {
public:
    bool initialize_success = true;
    bool initialize_called = false;
    bool shutdown_called = false;
    bool auto_progress_connection = true;
    int tick_count = 0;
    int connect_request_count = 0;
    int disconnect_request_count = 0;
    std::string last_transition_reason = "initialize";
    std::vector<novaria::net::PlayerCommand> submitted_commands;
    std::vector<std::pair<std::uint64_t, std::size_t>> published_snapshots;
    std::vector<std::vector<std::string>> published_snapshot_payloads;
    std::vector<std::string> pending_remote_chunk_payloads;
    novaria::net::NetSessionState session_state = novaria::net::NetSessionState::Disconnected;

    bool Initialize(std::string& out_error) override {
        initialize_called = true;
        if (!initialize_success) {
            out_error = "fake net init failed";
            return false;
        }
        out_error.clear();
        return true;
    }

    void Shutdown() override {
        shutdown_called = true;
    }

    void RequestConnect() override {
        ++connect_request_count;
        if (session_state == novaria::net::NetSessionState::Disconnected) {
            session_state = novaria::net::NetSessionState::Connecting;
            last_transition_reason = "request_connect";
        }
    }

    void RequestDisconnect() override {
        ++disconnect_request_count;
        session_state = novaria::net::NetSessionState::Disconnected;
        last_transition_reason = "request_disconnect";
    }

    void NotifyHeartbeatReceived(std::uint64_t tick_index) override {
        (void)tick_index;
    }

    novaria::net::NetSessionState SessionState() const override {
        return session_state;
    }

    novaria::net::NetDiagnosticsSnapshot DiagnosticsSnapshot() const override {
        return novaria::net::NetDiagnosticsSnapshot{
            .session_state = session_state,
            .last_session_transition_reason = last_transition_reason,
        };
    }

    void Tick(const novaria::sim::TickContext& tick_context) override {
        (void)tick_context;
        if (session_state == novaria::net::NetSessionState::Connecting &&
            auto_progress_connection) {
            session_state = novaria::net::NetSessionState::Connected;
            last_transition_reason = "tick_connect_complete";
        }
        ++tick_count;
    }

    void SubmitLocalCommand(const novaria::net::PlayerCommand& command) override {
        submitted_commands.push_back(command);
    }

    std::vector<std::string> ConsumeRemoteChunkPayloads() override {
        std::vector<std::string> payloads = std::move(pending_remote_chunk_payloads);
        pending_remote_chunk_payloads.clear();
        return payloads;
    }

    void PublishWorldSnapshot(
        std::uint64_t tick_index,
        const std::vector<std::string>& encoded_dirty_chunks) override {
        published_snapshots.emplace_back(tick_index, encoded_dirty_chunks.size());
        published_snapshot_payloads.push_back(encoded_dirty_chunks);
    }
};

class FakeScriptHost final : public novaria::script::IScriptHost {
public:
    bool initialize_success = true;
    bool initialize_called = false;
    bool shutdown_called = false;
    int tick_count = 0;
    std::vector<novaria::script::ScriptEvent> dispatched_events;

    bool Initialize(std::string& out_error) override {
        initialize_called = true;
        if (!initialize_success) {
            out_error = "fake script init failed";
            return false;
        }
        out_error.clear();
        return true;
    }

    void Shutdown() override {
        shutdown_called = true;
    }

    void Tick(const novaria::sim::TickContext& tick_context) override {
        (void)tick_context;
        ++tick_count;
    }

    void DispatchEvent(const novaria::script::ScriptEvent& event_data) override {
        dispatched_events.push_back(event_data);
    }

    novaria::script::ScriptRuntimeDescriptor RuntimeDescriptor() const override {
        return novaria::script::ScriptRuntimeDescriptor{
            .backend_name = "fake",
            .api_version = novaria::script::kScriptApiVersion,
            .sandbox_enabled = false,
        };
    }
};

bool TestCommandSchemaPayloadParsing() {
    bool passed = true;

    novaria::sim::command::WorldSetTilePayload set_tile_payload{};
    passed &= Expect(
        novaria::sim::command::TryParseWorldSetTilePayload(
            novaria::sim::command::BuildWorldSetTilePayload(-12, 34, 7),
            set_tile_payload),
        "World set_tile payload parser should accept valid payload.");
    passed &= Expect(
        set_tile_payload.tile_x == -12 &&
            set_tile_payload.tile_y == 34 &&
            set_tile_payload.material_id == 7,
        "Parsed set_tile payload fields should match.");
    passed &= Expect(
        !novaria::sim::command::TryParseWorldSetTilePayload("1,2", set_tile_payload),
        "Set_tile payload parser should reject missing tokens.");
    passed &= Expect(
        !novaria::sim::command::TryParseWorldSetTilePayload("1,2,3,4", set_tile_payload),
        "Set_tile payload parser should reject extra tokens.");
    passed &= Expect(
        !novaria::sim::command::TryParseWorldSetTilePayload("1,2,70000", set_tile_payload),
        "Set_tile payload parser should reject material_id overflow.");

    novaria::sim::command::WorldChunkPayload chunk_payload{};
    passed &= Expect(
        novaria::sim::command::TryParseWorldChunkPayload(
            novaria::sim::command::BuildWorldChunkPayload(5, -9),
            chunk_payload),
        "World chunk payload parser should accept valid payload.");
    passed &= Expect(
        chunk_payload.chunk_x == 5 && chunk_payload.chunk_y == -9,
        "Parsed chunk payload fields should match.");
    passed &= Expect(
        !novaria::sim::command::TryParseWorldChunkPayload("8", chunk_payload),
        "Chunk payload parser should reject missing tokens.");
    passed &= Expect(
        !novaria::sim::command::TryParseWorldChunkPayload("8,9,10", chunk_payload),
        "Chunk payload parser should reject extra tokens.");
    passed &= Expect(
        !novaria::sim::command::TryParseWorldChunkPayload("8,NaN", chunk_payload),
        "Chunk payload parser should reject non-number tokens.");

    novaria::sim::command::CollectResourcePayload collect_payload{};
    passed &= Expect(
        novaria::sim::command::TryParseCollectResourcePayload(
            novaria::sim::command::BuildCollectResourcePayload(
                novaria::sim::command::kResourceWood,
                5),
            collect_payload),
        "Collect payload parser should accept valid payload.");
    passed &= Expect(
        collect_payload.resource_id == novaria::sim::command::kResourceWood &&
            collect_payload.amount == 5,
        "Collect payload parser should parse resource and amount.");
    passed &= Expect(
        !novaria::sim::command::TryParseCollectResourcePayload("1,0", collect_payload),
        "Collect payload parser should reject zero amount.");
    passed &= Expect(
        !novaria::sim::command::TryParseCollectResourcePayload("x,3", collect_payload),
        "Collect payload parser should reject invalid resource id.");
    passed &= Expect(
        !novaria::sim::command::TryParseCollectResourcePayload("1,2,3", collect_payload),
        "Collect payload parser should reject extra tokens.");

    return passed;
}

bool TestSessionStateChangedPayloadParser() {
    bool passed = true;

    SessionStateChangedPayload payload{};
    passed &= Expect(
        TryParseSessionStateChangedPayload(
            "state=connected;tick=17;reason=tick_connect_complete",
            payload),
        "Session state payload parser should accept valid payload.");
    passed &= Expect(
        payload.state == "connected" && payload.tick_index == 17 &&
            payload.reason == "tick_connect_complete",
        "Session state payload parser should return structured fields.");
    passed &= Expect(
        !TryParseSessionStateChangedPayload("connected,17,tick_connect_complete", payload),
        "Session state payload parser should reject legacy CSV payload.");
    passed &= Expect(
        !TryParseSessionStateChangedPayload(
            "state=connected;tick=nan;reason=tick_connect_complete",
            payload),
        "Session state payload parser should reject non-numeric tick.");
    passed &= Expect(
        !TryParseSessionStateChangedPayload(
            "state=connected;tick=17",
            payload),
        "Session state payload parser should reject missing reason token.");

    return passed;
}

bool TestUpdatePublishesDirtyChunkCount() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    world.dirty_batches = {
        {{.x = 0, .y = 0}, {.x = 1, .y = 0}},
        {{.x = -1, .y = -1}},
    };
    world.available_snapshots = {
        {.chunk_coord = {.x = 0, .y = 0}, .tiles = {1, 2, 3}},
        {.chunk_coord = {.x = 1, .y = 0}, .tiles = {3, 4, 5}},
        {.chunk_coord = {.x = -1, .y = -1}, .tiles = {6, 7, 8}},
    };

    novaria::sim::SimulationKernel kernel(world, net, script);
    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(error.empty(), "Kernel initialize should not return error.");

    kernel.Update(1.0 / 60.0);
    kernel.Update(1.0 / 60.0);

    passed &= Expect(world.tick_count == 2, "World tick should run twice.");
    passed &= Expect(net.tick_count == 2, "Net tick should run twice.");
    passed &= Expect(script.tick_count == 2, "Script tick should run twice.");
    passed &= Expect(net.published_snapshots.size() == 2, "Two snapshots should be published.");

    if (net.published_snapshots.size() == 2) {
        passed &= Expect(net.published_snapshots[0].first == 0, "First snapshot tick should be 0.");
        passed &= Expect(net.published_snapshots[0].second == 2, "First snapshot dirty chunk count should be 2.");
        passed &= Expect(net.published_snapshots[1].first == 1, "Second snapshot tick should be 1.");
        passed &= Expect(net.published_snapshots[1].second == 1, "Second snapshot dirty chunk count should be 1.");
        passed &= Expect(
            net.published_snapshot_payloads[0].size() == 2,
            "First snapshot payload should contain two chunk entries.");
        novaria::world::ChunkSnapshot decoded_snapshot{};
        std::string decode_error;
        passed &= Expect(
            novaria::world::WorldSnapshotCodec::DecodeChunkSnapshot(
                net.published_snapshot_payloads[0][0],
                decoded_snapshot,
                decode_error),
            "Encoded chunk payload should be decodable.");
    }

    kernel.SubmitLocalCommand({
        .player_id = 12,
        .command_type = std::string(novaria::sim::command::kJump),
        .payload = "",
    });
    kernel.SubmitLocalCommand({
        .player_id = 12,
        .command_type = std::string(novaria::sim::command::kAttack),
        .payload = "light",
    });
    kernel.Update(1.0 / 60.0);
    passed &= Expect(net.submitted_commands.size() == 2, "Submitted commands should be forwarded on update.");
    if (net.submitted_commands.size() == 2) {
        passed &= Expect(
            net.submitted_commands[0].command_type == novaria::sim::command::kJump,
            "First command type should match.");
        passed &= Expect(
            net.submitted_commands[1].command_type == novaria::sim::command::kAttack,
            "Second command type should match.");
    }

    kernel.Shutdown();
    passed &= Expect(script.shutdown_called, "Script shutdown should be called.");
    passed &= Expect(net.shutdown_called, "Net shutdown should be called.");
    passed &= Expect(world.shutdown_called, "World shutdown should be called.");
    return passed;
}

bool TestInitializeRollbackOnNetFailure() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.initialize_success = false;

    novaria::sim::SimulationKernel kernel(world, net, script);
    std::string error;
    passed &= Expect(!kernel.Initialize(error), "Kernel initialize should fail if net initialize fails.");
    passed &= Expect(world.initialize_called, "World initialize should be called.");
    passed &= Expect(net.initialize_called, "Net initialize should be called.");
    passed &= Expect(!script.initialize_called, "Script initialize should not run after net failure.");
    passed &= Expect(world.shutdown_called, "World should rollback via shutdown.");
    passed &= Expect(!net.shutdown_called, "Net shutdown should not be called when net init fails.");
    return passed;
}

bool TestInitializeRollbackOnScriptFailure() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    script.initialize_success = false;

    novaria::sim::SimulationKernel kernel(world, net, script);
    std::string error;
    passed &= Expect(!kernel.Initialize(error), "Kernel initialize should fail if script initialize fails.");
    passed &= Expect(world.initialize_called, "World initialize should be called.");
    passed &= Expect(net.initialize_called, "Net initialize should be called.");
    passed &= Expect(script.initialize_called, "Script initialize should be called.");
    passed &= Expect(net.shutdown_called, "Net should rollback via shutdown.");
    passed &= Expect(world.shutdown_called, "World should rollback via shutdown.");
    return passed;
}

bool TestInitializeRequestsNetConnect() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(net.connect_request_count == 1, "Kernel initialize should request one net connect.");
    passed &= Expect(
        net.SessionState() == novaria::net::NetSessionState::Connecting,
        "Fake net should enter connecting state after connect request.");

    kernel.Shutdown();
    return passed;
}

bool TestUpdateRequestsReconnectWhenNetDisconnected() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(net.connect_request_count == 1, "Initialize should request initial net connect.");

    net.session_state = novaria::net::NetSessionState::Disconnected;
    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        net.connect_request_count == 2,
        "Kernel update should request reconnect when net session is disconnected.");
    passed &= Expect(
        net.SessionState() == novaria::net::NetSessionState::Connected,
        "Reconnect request should recover fake net session.");

    kernel.Shutdown();
    return passed;
}

bool TestReconnectRequestsAreRateLimitedByTickInterval() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.auto_progress_connection = false;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(net.connect_request_count == 1, "Initialize should request initial net connect.");

    net.session_state = novaria::net::NetSessionState::Disconnected;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        net.connect_request_count == 2,
        "First disconnected update should request reconnect.");

    net.session_state = novaria::net::NetSessionState::Disconnected;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        net.connect_request_count == 2,
        "Reconnect request should be rate-limited before interval is reached.");

    while (kernel.CurrentTick() <
           novaria::sim::SimulationKernel::kAutoReconnectRetryIntervalTicks + 1) {
        net.session_state = novaria::net::NetSessionState::Disconnected;
        kernel.Update(1.0 / 60.0);
    }
    passed &= Expect(
        net.connect_request_count == 2,
        "Reconnect count should remain unchanged before recalculated retry interval boundary.");

    net.session_state = novaria::net::NetSessionState::Disconnected;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        net.connect_request_count == 3,
        "Reconnect should trigger once retry interval boundary is reached.");

    kernel.Shutdown();
    return passed;
}

bool TestNetSessionStateChangeDispatchesScriptEvent() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.auto_progress_connection = false;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(
        script.dispatched_events.empty(),
        "Kernel initialize should not dispatch session change event yet.");

    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        script.dispatched_events.empty(),
        "No session change event should dispatch when state remains connecting.");

    net.auto_progress_connection = true;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        script.dispatched_events.size() == 1,
        "Session change to connected should dispatch one script event.");
    if (script.dispatched_events.size() == 1) {
        SessionStateChangedPayload payload{};
        passed &= Expect(
            script.dispatched_events[0].event_name == "net.session_state_changed",
            "Session change event name should match contract.");
        passed &= Expect(
            TryParseSessionStateChangedPayload(script.dispatched_events[0].payload, payload),
            "Session change event payload should be parseable in KV format.");
        passed &= Expect(
            payload.state == "connected" && payload.tick_index == 1 &&
                payload.reason == "tick_connect_complete",
            "Connected transition payload fields should match.");
    }

    net.auto_progress_connection = false;
    net.session_state = novaria::net::NetSessionState::Disconnected;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        script.dispatched_events.size() == 1,
        "Reconnect transition should be throttled during session-event cooldown.");

    const std::uint64_t reconnect_event_tick =
        1 + novaria::sim::SimulationKernel::kSessionStateEventMinIntervalTicks;
    while (kernel.CurrentTick() <= reconnect_event_tick) {
        kernel.Update(1.0 / 60.0);
    }

    passed &= Expect(
        script.dispatched_events.size() == 2,
        "Reconnect transition should dispatch after session-event cooldown.");
    if (script.dispatched_events.size() == 2) {
        SessionStateChangedPayload payload{};
        passed &= Expect(
            TryParseSessionStateChangedPayload(script.dispatched_events[1].payload, payload),
            "Reconnect transition payload should be parseable in KV format.");
        passed &= Expect(
            payload.state == "connecting" && payload.tick_index == 2 &&
                payload.reason == "request_connect",
            "Reconnect transition payload fields should match.");
    }

    kernel.Shutdown();
    return passed;
}

bool TestNetSessionStateEventsAreCoalescedWithinCooldown() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.auto_progress_connection = false;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    kernel.Update(1.0 / 60.0);
    net.auto_progress_connection = true;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        script.dispatched_events.size() == 1,
        "Connected transition should dispatch immediately.");

    net.auto_progress_connection = false;
    net.session_state = novaria::net::NetSessionState::Disconnected;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        script.dispatched_events.size() == 1,
        "Connecting transition should remain pending within cooldown.");

    net.auto_progress_connection = true;
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        script.dispatched_events.size() == 1,
        "Latest transition should still be queued within cooldown.");

    const std::uint64_t coalesced_event_tick =
        1 + novaria::sim::SimulationKernel::kSessionStateEventMinIntervalTicks;
    while (kernel.CurrentTick() <= coalesced_event_tick) {
        kernel.Update(1.0 / 60.0);
    }

    passed &= Expect(
        script.dispatched_events.size() == 2,
        "Cooldown boundary should flush one coalesced transition event.");
    if (script.dispatched_events.size() == 2) {
        SessionStateChangedPayload payload{};
        passed &= Expect(
            TryParseSessionStateChangedPayload(script.dispatched_events[1].payload, payload),
            "Coalesced transition payload should be parseable.");
        passed &= Expect(
            payload.state == "connected" && payload.tick_index == 3 &&
                payload.reason == "tick_connect_complete",
            "Coalesced transition should keep the latest state change.");
    }

    kernel.Shutdown();
    return passed;
}

bool TestReconnectHeartbeatAndSaveDiagnosticsEndToEnd() {
    bool passed = true;
    const std::filesystem::path test_dir = BuildSimulationKernelSaveTestDirectory();
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    FakeWorldService world;
    novaria::net::NetServiceUdpLoopback net;
    net.SetBindPort(0);
    net.SetRemoteEndpoint({.host = "127.0.0.1", .port = 0});
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    for (std::uint64_t tick = 0; tick < 20; ++tick) {
        kernel.Update(1.0 / 60.0);
        if (!script.dispatched_events.empty()) {
            break;
        }
    }
    passed &= Expect(
        script.dispatched_events.size() == 1,
        "Initial connect transition should dispatch one script event.");
    SessionStateChangedPayload initial_connect_payload{};
    if (!script.dispatched_events.empty()) {
        passed &= Expect(
            TryParseSessionStateChangedPayload(
                script.dispatched_events[0].payload,
                initial_connect_payload),
            "Initial connect payload should be parseable.");
        passed &= Expect(
            initial_connect_payload.state == "connected" &&
                initial_connect_payload.reason == "udp_handshake_ack",
            "Initial connect payload fields should match expected transition.");
    }

    const std::uint16_t local_port = net.LocalPort();
    const std::uint16_t dead_port = local_port == 65535 ? 65534 : static_cast<std::uint16_t>(local_port + 1);
    net.SetRemoteEndpoint({.host = "127.0.0.1", .port = dead_port});

    while (net.DiagnosticsSnapshot().timeout_disconnect_count == 0 && kernel.CurrentTick() < 2000) {
        kernel.Update(1.0 / 60.0);
    }
    passed &= Expect(net.DiagnosticsSnapshot().timeout_disconnect_count == 1, "Heartbeat timeout should occur.");

    passed &= Expect(
        script.dispatched_events.size() >= 2,
        "Heartbeat timeout should dispatch disconnect event.");
    SessionStateChangedPayload disconnect_payload{};
    if (script.dispatched_events.size() >= 2) {
        passed &= Expect(
            TryParseSessionStateChangedPayload(script.dispatched_events[1].payload, disconnect_payload),
            "Disconnect payload should be parseable.");
        passed &= Expect(
            disconnect_payload.state == "disconnected" &&
                disconnect_payload.reason == "heartbeat_timeout",
            "Disconnect payload fields should match heartbeat timeout transition.");
    }

    net.SetRemoteEndpoint({.host = "127.0.0.1", .port = local_port});

    while (net.DiagnosticsSnapshot().connected_transition_count < 2 && kernel.CurrentTick() < 10000) {
        kernel.Update(1.0 / 60.0);
    }
    passed &= Expect(
        net.DiagnosticsSnapshot().connected_transition_count >= 2,
        "Auto reconnect should eventually restore connected state.");
    for (std::uint64_t tick = 0; tick < novaria::sim::SimulationKernel::kSessionStateEventMinIntervalTicks + 2;
         ++tick) {
        kernel.Update(1.0 / 60.0);
    }

    passed &= Expect(
        script.dispatched_events.size() >= 3,
        "Auto reconnect should dispatch connected event.");
    bool found_reconnect_event = false;
    for (std::size_t event_index = 2; event_index < script.dispatched_events.size(); ++event_index) {
        SessionStateChangedPayload reconnect_payload{};
        if (!TryParseSessionStateChangedPayload(
                script.dispatched_events[event_index].payload,
                reconnect_payload)) {
            continue;
        }
        if (reconnect_payload.state == "connected") {
            found_reconnect_event = true;
            break;
        }
    }
    passed &= Expect(found_reconnect_event, "Reconnect flow should include a connected session event.");

    const std::uint64_t heartbeat_recovery_tick = kernel.CurrentTick();
    net.NotifyHeartbeatReceived(heartbeat_recovery_tick);
    kernel.Update(1.0 / 60.0);

    const novaria::net::NetDiagnosticsSnapshot diagnostics = net.DiagnosticsSnapshot();
    passed &= Expect(
        diagnostics.session_state == novaria::net::NetSessionState::Connected,
        "Net diagnostics should report connected after reconnect.");
    passed &= Expect(
        diagnostics.timeout_disconnect_count == 1,
        "Net diagnostics should report one heartbeat timeout disconnect.");
    passed &= Expect(
        diagnostics.connected_transition_count == 2,
        "Net diagnostics should report two connected transitions.");
    passed &= Expect(
        diagnostics.last_heartbeat_tick == heartbeat_recovery_tick,
        "Net diagnostics should record restored heartbeat tick.");

    novaria::save::FileSaveRepository save_repository;
    passed &= Expect(
        save_repository.Initialize(test_dir, error),
        "Save repository initialize should succeed.");
    const novaria::save::WorldSaveState expected_save_state{
        .tick_index = kernel.CurrentTick(),
        .local_player_id = 42,
        .mod_manifest_fingerprint = "mods:v1:e2e",
        .debug_net_session_transitions = diagnostics.session_transition_count,
        .debug_net_timeout_disconnects = diagnostics.timeout_disconnect_count,
        .debug_net_manual_disconnects = diagnostics.manual_disconnect_count,
        .debug_net_last_heartbeat_tick = diagnostics.last_heartbeat_tick,
        .debug_net_dropped_commands = diagnostics.dropped_command_count,
        .debug_net_dropped_remote_payloads = diagnostics.dropped_remote_chunk_payload_count,
        .debug_net_last_transition_reason = diagnostics.last_session_transition_reason,
    };
    passed &= Expect(
        save_repository.SaveWorldState(expected_save_state, error),
        "Save repository should persist e2e diagnostics snapshot.");

    novaria::save::WorldSaveState loaded_save_state{};
    passed &= Expect(
        save_repository.LoadWorldState(loaded_save_state, error),
        "Save repository should load persisted e2e diagnostics snapshot.");
    passed &= Expect(
        loaded_save_state.debug_net_session_transitions ==
            expected_save_state.debug_net_session_transitions,
        "Loaded session transition count should match persisted diagnostics.");
    passed &= Expect(
        loaded_save_state.debug_net_timeout_disconnects == 1,
        "Loaded timeout disconnect count should match expected reconnect flow.");
    passed &= Expect(
        loaded_save_state.debug_net_last_heartbeat_tick == heartbeat_recovery_tick,
        "Loaded last heartbeat tick should match recovered heartbeat.");
    passed &= Expect(
        loaded_save_state.debug_net_last_transition_reason == diagnostics.last_session_transition_reason,
        "Loaded last transition reason should align with diagnostics snapshot.");
    passed &= Expect(!loaded_save_state.debug_net_last_transition_reason.empty(), "Last transition reason should persist.");

    save_repository.Shutdown();
    kernel.Shutdown();
    std::filesystem::remove_all(test_dir, ec);
    return passed;
}

bool TestSubmitCommandIgnoredBeforeInitialize() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    kernel.SubmitLocalCommand({.player_id = 3, .command_type = "move", .payload = "left"});
    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        net.submitted_commands.empty(),
        "Command submitted before initialize should be ignored.");
    kernel.Shutdown();
    return passed;
}

bool TestLocalCommandQueueCapAndDroppedCount() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    constexpr std::size_t overflow_count = 5;
    for (std::size_t index = 0;
         index < novaria::sim::SimulationKernel::kMaxPendingLocalCommands + overflow_count;
         ++index) {
        kernel.SubmitLocalCommand({
            .player_id = 99,
            .command_type = std::string(novaria::sim::command::kJump),
            .payload = "",
        });
    }

    passed &= Expect(
        kernel.PendingLocalCommandCount() == novaria::sim::SimulationKernel::kMaxPendingLocalCommands,
        "Pending local command count should cap at configured max.");
    passed &= Expect(
        kernel.DroppedLocalCommandCount() == overflow_count,
        "Dropped local command count should track overflow commands.");

    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        net.submitted_commands.size() == novaria::sim::SimulationKernel::kMaxPendingLocalCommands,
        "Kernel should forward only capped local commands.");
    passed &= Expect(
        kernel.PendingLocalCommandCount() == 0,
        "Pending local command queue should be cleared after update.");
    passed &= Expect(
        kernel.DroppedLocalCommandCount() == overflow_count,
        "Dropped local command count should persist across update.");

    kernel.SubmitLocalCommand({
        .player_id = 99,
        .command_type = std::string(novaria::sim::command::kJump),
        .payload = "",
    });
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        net.submitted_commands.size() == novaria::sim::SimulationKernel::kMaxPendingLocalCommands + 1,
        "Kernel should accept new local commands after queue drain.");

    kernel.Shutdown();
    return passed;
}

bool TestApplyRemoteChunkPayload() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(
        !kernel.ApplyRemoteChunkPayload("0,0,1,1", error),
        "ApplyRemoteChunkPayload should fail before initialize.");

    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(
        !kernel.ApplyRemoteChunkPayload("invalid_payload", error),
        "ApplyRemoteChunkPayload should fail for invalid payload.");

    novaria::world::ChunkSnapshot snapshot{
        .chunk_coord = {.x = 2, .y = -3},
        .tiles = {1, 2, 3, 4},
    };
    std::string payload;
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(snapshot, payload, error),
        "EncodeChunkSnapshot should succeed.");
    passed &= Expect(
        kernel.ApplyRemoteChunkPayload(payload, error),
        "ApplyRemoteChunkPayload should accept valid payload.");
    passed &= Expect(world.applied_snapshots.size() == 1, "World should receive one applied snapshot.");
    if (world.applied_snapshots.size() == 1) {
        passed &= Expect(
            world.applied_snapshots[0].chunk_coord.x == 2 &&
                world.applied_snapshots[0].chunk_coord.y == -3,
            "Applied snapshot chunk coordinate should match.");
        passed &= Expect(
            world.applied_snapshots[0].tiles == snapshot.tiles,
            "Applied snapshot tile data should match.");
    }

    kernel.Shutdown();
    return passed;
}

bool TestUpdateConsumesRemoteChunkPayloads() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    novaria::world::ChunkSnapshot snapshot{
        .chunk_coord = {.x = -4, .y = 9},
        .tiles = {11, 12, 13, 14},
    };
    std::string payload;
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(snapshot, payload, error),
        "EncodeChunkSnapshot should succeed.");
    net.pending_remote_chunk_payloads.push_back(payload);
    net.pending_remote_chunk_payloads.push_back("invalid_payload");

    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        world.applied_snapshots.size() == 1,
        "Kernel update should apply one valid remote chunk payload.");
    if (world.applied_snapshots.size() == 1) {
        passed &= Expect(
            world.applied_snapshots[0].chunk_coord.x == -4 &&
                world.applied_snapshots[0].chunk_coord.y == 9,
            "Applied remote snapshot chunk coordinate should match.");
    }
    passed &= Expect(
        net.pending_remote_chunk_payloads.empty(),
        "Remote payload queue should be drained after update.");

    kernel.Shutdown();
    return passed;
}

bool TestUpdateSkipsNetExchangeWhenSessionNotConnected() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.auto_progress_connection = false;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(
        net.SessionState() == novaria::net::NetSessionState::Connecting,
        "Net session should remain connecting when auto-progress is disabled.");

    world.dirty_batches = {
        {{.x = 3, .y = 4}},
    };
    world.available_snapshots = {
        {.chunk_coord = {.x = 3, .y = 4}, .tiles = {8, 8, 8, 8}},
    };

    novaria::world::ChunkSnapshot remote_snapshot{
        .chunk_coord = {.x = 7, .y = -2},
        .tiles = {5, 6, 7, 8},
    };
    std::string remote_payload;
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(remote_snapshot, remote_payload, error),
        "EncodeChunkSnapshot should succeed.");
    net.pending_remote_chunk_payloads.push_back(remote_payload);

    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        world.applied_snapshots.empty(),
        "Kernel should not apply remote payloads when net session is not connected.");
    passed &= Expect(
        net.pending_remote_chunk_payloads.size() == 1,
        "Remote payload queue should remain untouched when not connected.");
    passed &= Expect(
        net.published_snapshots.empty(),
        "Kernel should not publish world snapshots when net session is not connected.");

    kernel.Shutdown();
    return passed;
}

bool TestWorldCommandExecutionFromLocalQueue() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kWorldLoadChunk),
        .payload = novaria::sim::command::BuildWorldChunkPayload(2, -1),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kWorldSetTile),
        .payload = novaria::sim::command::BuildWorldSetTilePayload(10, 11, 7),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kWorldUnloadChunk),
        .payload = novaria::sim::command::BuildWorldChunkPayload(2, -1),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kWorldSetTile),
        .payload = "invalid",
    });
    kernel.Update(1.0 / 60.0);

    passed &= Expect(net.submitted_commands.size() == 4, "All local commands should be forwarded to net.");
    passed &= Expect(world.loaded_chunks.size() == 1, "One load chunk command should execute.");
    if (world.loaded_chunks.size() == 1) {
        passed &= Expect(
            world.loaded_chunks[0].x == 2 && world.loaded_chunks[0].y == -1,
            "Loaded chunk coordinates should match command payload.");
    }
    passed &= Expect(world.unloaded_chunks.size() == 1, "One unload chunk command should execute.");
    passed &= Expect(world.applied_tile_mutations.size() == 1, "Only valid set_tile command should execute.");
    if (world.applied_tile_mutations.size() == 1) {
        passed &= Expect(
            world.applied_tile_mutations[0].tile_x == 10 &&
                world.applied_tile_mutations[0].tile_y == 11 &&
                world.applied_tile_mutations[0].material_id == 7,
            "Parsed tile mutation should match payload.");
    }

    kernel.Shutdown();
    return passed;
}

bool TestGameplayLoopCommandsReachBossDefeat() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.auto_progress_connection = false;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kGameplayCollectResource),
        .payload = novaria::sim::command::BuildCollectResourcePayload(
            novaria::sim::command::kResourceWood,
            20),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kGameplayCollectResource),
        .payload = novaria::sim::command::BuildCollectResourcePayload(
            novaria::sim::command::kResourceStone,
            20),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kGameplayBuildWorkbench),
        .payload = "",
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_type = std::string(novaria::sim::command::kGameplayCraftSword),
        .payload = "",
    });
    for (int index = 0; index < 3; ++index) {
        kernel.SubmitLocalCommand({
            .player_id = 1,
            .command_type = std::string(novaria::sim::command::kGameplayAttackEnemy),
            .payload = "",
        });
    }
    for (int index = 0; index < 6; ++index) {
        kernel.SubmitLocalCommand({
            .player_id = 1,
            .command_type = std::string(novaria::sim::command::kGameplayAttackBoss),
            .payload = "",
        });
    }

    kernel.Update(1.0 / 60.0);
    const novaria::sim::GameplayProgressSnapshot progress = kernel.GameplayProgress();

    passed &= Expect(
        progress.wood_collected == 2 && progress.stone_collected == 3,
        "Gameplay resources should deduct build and craft costs.");
    passed &= Expect(progress.workbench_built, "Gameplay loop should build workbench.");
    passed &= Expect(progress.sword_crafted, "Gameplay loop should craft sword.");
    passed &= Expect(progress.enemy_kill_count == 3, "Gameplay loop should record three enemy kills.");
    passed &= Expect(progress.boss_health == 0 && progress.boss_defeated, "Gameplay loop should defeat boss.");
    passed &= Expect(progress.playable_loop_complete, "Gameplay loop should mark playable loop completion.");
    passed &= Expect(
        net.submitted_commands.size() == 13,
        "Gameplay commands should still be forwarded to net command stream.");

    bool saw_playable_loop_complete_event = false;
    for (const auto& event : script.dispatched_events) {
        if (event.event_name != "gameplay.progress") {
            continue;
        }

        GameplayProgressPayload payload{};
        passed &= Expect(
            TryParseGameplayProgressPayload(event.payload, payload),
            "Gameplay progress event payload should be parseable.");
        if (payload.milestone == "playable_loop_complete") {
            saw_playable_loop_complete_event = true;
            passed &= Expect(
                payload.tick_index == 0,
                "Playable loop complete milestone should be emitted at processing tick.");
        }
    }
    passed &= Expect(
        saw_playable_loop_complete_event,
        "Gameplay loop should emit playable loop completion milestone event.");

    kernel.Shutdown();
    return passed;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= TestCommandSchemaPayloadParsing();
    passed &= TestSessionStateChangedPayloadParser();
    passed &= TestUpdatePublishesDirtyChunkCount();
    passed &= TestInitializeRollbackOnNetFailure();
    passed &= TestInitializeRollbackOnScriptFailure();
    passed &= TestInitializeRequestsNetConnect();
    passed &= TestUpdateRequestsReconnectWhenNetDisconnected();
    passed &= TestReconnectRequestsAreRateLimitedByTickInterval();
    passed &= TestNetSessionStateChangeDispatchesScriptEvent();
    passed &= TestNetSessionStateEventsAreCoalescedWithinCooldown();
    passed &= TestReconnectHeartbeatAndSaveDiagnosticsEndToEnd();
    passed &= TestSubmitCommandIgnoredBeforeInitialize();
    passed &= TestLocalCommandQueueCapAndDroppedCount();
    passed &= TestApplyRemoteChunkPayload();
    passed &= TestWorldCommandExecutionFromLocalQueue();
    passed &= TestGameplayLoopCommandsReachBossDefeat();
    passed &= TestUpdateConsumesRemoteChunkPayloads();
    passed &= TestUpdateSkipsNetExchangeWhenSessionNotConnected();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_simulation_kernel_tests\n";
    return 0;
}
