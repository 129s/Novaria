#include "sim/simulation_kernel.h"
#include "sim/command_schema.h"
#include "world/snapshot_codec.h"

#include <cstdint>
#include <iostream>
#include <string>
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
    int tick_count = 0;
    std::vector<novaria::net::PlayerCommand> submitted_commands;
    std::vector<std::pair<std::uint64_t, std::size_t>> published_snapshots;
    std::vector<std::vector<std::string>> published_snapshot_payloads;
    std::vector<std::string> pending_remote_chunk_payloads;

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

    void Tick(const novaria::sim::TickContext& tick_context) override {
        (void)tick_context;
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
        (void)event_data;
    }
};

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

}  // namespace

int main() {
    bool passed = true;
    passed &= TestUpdatePublishesDirtyChunkCount();
    passed &= TestInitializeRollbackOnNetFailure();
    passed &= TestInitializeRollbackOnScriptFailure();
    passed &= TestSubmitCommandIgnoredBeforeInitialize();
    passed &= TestApplyRemoteChunkPayload();
    passed &= TestWorldCommandExecutionFromLocalQueue();
    passed &= TestUpdateConsumesRemoteChunkPayloads();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_simulation_kernel_tests\n";
    return 0;
}
