#include "sim/simulation_kernel.h"

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

class FakeWorldService final : public novaria::world::IWorldService {
public:
    bool initialize_success = true;
    bool initialize_called = false;
    bool shutdown_called = false;
    int tick_count = 0;
    std::vector<std::vector<novaria::world::ChunkCoord>> dirty_batches;
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
        (void)chunk_coord;
    }

    void UnloadChunk(const novaria::world::ChunkCoord& chunk_coord) override {
        (void)chunk_coord;
    }

    bool ApplyTileMutation(const novaria::world::TileMutation& mutation, std::string& out_error) override {
        (void)mutation;
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

    void PublishWorldSnapshot(std::uint64_t tick_index, std::size_t dirty_chunk_count) override {
        published_snapshots.emplace_back(tick_index, dirty_chunk_count);
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
    }

    kernel.SubmitLocalCommand({.player_id = 12, .command_type = "jump", .payload = ""});
    kernel.SubmitLocalCommand({.player_id = 12, .command_type = "attack", .payload = "light"});
    kernel.Update(1.0 / 60.0);
    passed &= Expect(net.submitted_commands.size() == 2, "Submitted commands should be forwarded on update.");
    if (net.submitted_commands.size() == 2) {
        passed &= Expect(net.submitted_commands[0].command_type == "jump", "First command type should match.");
        passed &= Expect(net.submitted_commands[1].command_type == "attack", "Second command type should match.");
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

}  // namespace

int main() {
    bool passed = true;
    passed &= TestUpdatePublishesDirtyChunkCount();
    passed &= TestInitializeRollbackOnNetFailure();
    passed &= TestInitializeRollbackOnScriptFailure();
    passed &= TestSubmitCommandIgnoredBeforeInitialize();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_simulation_kernel_tests\n";
    return 0;
}
