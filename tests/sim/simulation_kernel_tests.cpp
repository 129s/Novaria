#include "sim/simulation_kernel.h"
#include "sim/command_schema.h"
#include "sim/typed_command.h"
#include "save/save_repository.h"
#include "net/net_service_udp_peer.h"
#include "script/sim_rules_rpc.h"
#include "world/snapshot_codec.h"
#include "world/material_catalog.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
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
    const auto unique_seed =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("novaria_sim_kernel_save_e2e_test_" + std::to_string(unique_seed));
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
    struct PairHash final {
        std::size_t operator()(const std::pair<int, int>& key) const {
            return std::hash<int>{}(key.first) ^ (std::hash<int>{}(key.second) << 1);
        }
    };

    std::unordered_map<std::pair<int, int>, std::uint16_t, PairHash> tiles;

    void SetTile(int tile_x, int tile_y, std::uint16_t material_id) {
        tiles[{tile_x, tile_y}] = material_id;
    }

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

    void Tick(const novaria::core::TickContext& tick_context) override {
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
        tiles[{mutation.tile_x, mutation.tile_y}] = mutation.material_id;
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

    bool TryReadTile(
        int tile_x,
        int tile_y,
        std::uint16_t& out_material_id) const override {
        const auto it = tiles.find({tile_x, tile_y});
        if (it == tiles.end()) {
            out_material_id = 0;
            return true;
        }

        out_material_id = it->second;
        return true;
    }

    std::vector<novaria::world::ChunkCoord> LoadedChunkCoords() const override {
        return loaded_chunks;
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
    std::vector<novaria::net::PlayerCommand> pending_remote_commands;
    std::vector<std::pair<std::uint64_t, std::size_t>> published_snapshots;
    std::vector<std::vector<novaria::wire::ByteBuffer>> published_snapshot_payloads;
    std::vector<novaria::wire::ByteBuffer> pending_remote_chunk_payloads;
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

    void Tick(const novaria::core::TickContext& tick_context) override {
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
        pending_remote_commands.push_back(command);
    }

    std::vector<novaria::net::PlayerCommand> ConsumeRemoteCommands() override {
        std::vector<novaria::net::PlayerCommand> commands = std::move(pending_remote_commands);
        pending_remote_commands.clear();
        return commands;
    }

    std::vector<novaria::wire::ByteBuffer> ConsumeRemoteChunkPayloads() override {
        std::vector<novaria::wire::ByteBuffer> payloads = std::move(pending_remote_chunk_payloads);
        pending_remote_chunk_payloads.clear();
        return payloads;
    }

    void PublishWorldSnapshot(
        std::uint64_t tick_index,
        const std::vector<novaria::wire::ByteBuffer>& encoded_dirty_chunks) override {
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

    bool SetScriptModules(
        std::vector<novaria::script::ScriptModuleSource> module_sources,
        std::string& out_error) override {
        (void)module_sources;
        out_error.clear();
        return true;
    }

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

    void Tick(const novaria::core::TickContext& tick_context) override {
        (void)tick_context;
        ++tick_count;
    }

    void DispatchEvent(const novaria::script::ScriptEvent& event_data) override {
        dispatched_events.push_back(event_data);
    }

    bool TryCallModuleFunction(
        std::string_view module_name,
        std::string_view function_name,
        novaria::wire::ByteSpan request_payload,
        novaria::wire::ByteBuffer& out_response_payload,
        std::string& out_error) override {
        (void)module_name;
        (void)function_name;

        out_response_payload.clear();
        const novaria::wire::ByteSpan request_bytes = request_payload;

        if (novaria::script::simrpc::TryDecodeValidateRequest(request_bytes)) {
            const novaria::wire::ByteBuffer response_bytes =
                novaria::script::simrpc::EncodeValidateResponse(true);
            out_response_payload = response_bytes;
            out_error.clear();
            return true;
        }

        novaria::script::simrpc::ActionPrimaryRequest action_request{};
        if (novaria::script::simrpc::TryDecodeActionPrimaryRequest(request_bytes, action_request)) {
            novaria::script::simrpc::ActionPrimaryResult result =
                novaria::script::simrpc::ActionPrimaryResult::Reject;
            novaria::script::simrpc::PlaceKind place_kind =
                novaria::script::simrpc::PlaceKind::None;
            std::uint32_t required_ticks = 0;

            if (action_request.hotbar_row == 0 &&
                action_request.hotbar_slot == 0 &&
                action_request.has_pickaxe_tool &&
                action_request.harvestable_by_pickaxe &&
                action_request.harvest_ticks > 0) {
                result = novaria::script::simrpc::ActionPrimaryResult::Harvest;
                required_ticks = action_request.harvest_ticks;
            } else if (action_request.hotbar_row == 0 &&
                action_request.hotbar_slot == 1 &&
                action_request.has_axe_tool &&
                action_request.harvestable_by_axe &&
                action_request.harvest_ticks > 0) {
                result = novaria::script::simrpc::ActionPrimaryResult::Harvest;
                required_ticks = action_request.harvest_ticks;
            } else if (action_request.hotbar_row == 0 &&
                action_request.hotbar_slot == 6 &&
                action_request.wood_sword_count > 0 &&
                action_request.harvestable_by_sword &&
                action_request.harvest_ticks > 0) {
                result = novaria::script::simrpc::ActionPrimaryResult::Harvest;
                required_ticks = action_request.harvest_ticks + 10;
            } else if (action_request.hotbar_row == 0 &&
                action_request.hotbar_slot == 2 &&
                action_request.target_is_air &&
                action_request.dirt_count > 0) {
                result = novaria::script::simrpc::ActionPrimaryResult::Place;
                place_kind = novaria::script::simrpc::PlaceKind::Dirt;
                required_ticks = 8;
            } else if (action_request.hotbar_row == 0 &&
                action_request.hotbar_slot == 3 &&
                action_request.target_is_air &&
                action_request.stone_count > 0) {
                result = novaria::script::simrpc::ActionPrimaryResult::Place;
                place_kind = novaria::script::simrpc::PlaceKind::Stone;
                required_ticks = 8;
            } else if (action_request.hotbar_row == 0 &&
                action_request.hotbar_slot == 4 &&
                action_request.target_is_air &&
                action_request.torch_count > 0) {
                result = novaria::script::simrpc::ActionPrimaryResult::Place;
                place_kind = novaria::script::simrpc::PlaceKind::Torch;
                required_ticks = 8;
            } else if (action_request.hotbar_row == 0 &&
                action_request.hotbar_slot == 5 &&
                action_request.target_is_air &&
                action_request.workbench_count > 0) {
                result = novaria::script::simrpc::ActionPrimaryResult::Place;
                place_kind = novaria::script::simrpc::PlaceKind::Workbench;
                required_ticks = 8;
            }

            const novaria::wire::ByteBuffer response_bytes =
                novaria::script::simrpc::EncodeActionPrimaryResponse(result, place_kind, required_ticks);
            out_response_payload = response_bytes;
            out_error.clear();
            return true;
        }

        novaria::script::simrpc::CraftRecipeRequest craft_request{};
        if (novaria::script::simrpc::TryDecodeCraftRecipeRequest(request_bytes, craft_request)) {
            novaria::script::simrpc::CraftRecipeResponse response{};

            if (craft_request.recipe_index == 0 && craft_request.wood_count >= 3) {
                response.result = novaria::script::simrpc::CraftRecipeResult::Craft;
                response.wood_delta = -3;
                response.workbench_delta = 1;
                response.crafted_kind = novaria::script::simrpc::CraftedKind::Workbench;
                response.mark_workbench_built = true;
            } else if (craft_request.recipe_index == 1 &&
                craft_request.wood_count >= 7 &&
                craft_request.workbench_reachable) {
                response.result = novaria::script::simrpc::CraftRecipeResult::Craft;
                response.wood_delta = -7;
                response.wood_sword_delta = 1;
                response.mark_sword_crafted = true;
            } else if (craft_request.recipe_index == 2 &&
                craft_request.wood_count >= 1 &&
                craft_request.coal_count >= 1) {
                response.result = novaria::script::simrpc::CraftRecipeResult::Craft;
                response.wood_delta = -1;
                response.coal_delta = -1;
                response.torch_delta = 4;
                response.crafted_kind = novaria::script::simrpc::CraftedKind::Torch;
            }

            const novaria::wire::ByteBuffer response_bytes =
                novaria::script::simrpc::EncodeCraftRecipeResponse(response);
            out_response_payload = response_bytes;
            out_error.clear();
            return true;
        }

        out_response_payload.clear();
        out_error = "fake script host received unknown simrpc payload";
        return false;
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
    const novaria::wire::ByteBuffer set_tile_encoded =
        novaria::sim::command::EncodeWorldSetTilePayload({.tile_x = -12, .tile_y = 34, .material_id = 7});
    passed &= Expect(
        novaria::sim::command::TryDecodeWorldSetTilePayload(
            novaria::wire::ByteSpan(set_tile_encoded.data(), set_tile_encoded.size()),
            set_tile_payload),
        "World set_tile payload decoder should accept valid payload.");
    passed &= Expect(
        set_tile_payload.tile_x == -12 &&
            set_tile_payload.tile_y == 34 &&
            set_tile_payload.material_id == 7,
        "Decoded set_tile payload fields should match.");
    novaria::wire::ByteBuffer set_tile_extra = set_tile_encoded;
    set_tile_extra.push_back(0x01);
    passed &= Expect(
        !novaria::sim::command::TryDecodeWorldSetTilePayload(
            novaria::wire::ByteSpan(set_tile_extra.data(), set_tile_extra.size()),
            set_tile_payload),
        "Set_tile decoder should reject trailing bytes.");

    novaria::wire::ByteWriter overflow_writer;
    overflow_writer.WriteVarInt(1);
    overflow_writer.WriteVarInt(2);
    overflow_writer.WriteVarUInt(70000);
    const novaria::wire::ByteBuffer material_overflow = overflow_writer.TakeBuffer();
    passed &= Expect(
        !novaria::sim::command::TryDecodeWorldSetTilePayload(
            novaria::wire::ByteSpan(material_overflow.data(), material_overflow.size()),
            set_tile_payload),
        "Set_tile decoder should reject material_id overflow.");

    novaria::sim::command::WorldChunkPayload chunk_payload{};
    const novaria::wire::ByteBuffer chunk_encoded =
        novaria::sim::command::EncodeWorldChunkPayload({.chunk_x = 5, .chunk_y = -9});
    passed &= Expect(
        novaria::sim::command::TryDecodeWorldChunkPayload(
            novaria::wire::ByteSpan(chunk_encoded.data(), chunk_encoded.size()),
            chunk_payload),
        "World chunk decoder should accept valid payload.");
    passed &= Expect(
        chunk_payload.chunk_x == 5 && chunk_payload.chunk_y == -9,
        "Decoded chunk payload fields should match.");
    passed &= Expect(
        !novaria::sim::command::TryDecodeWorldChunkPayload(
            novaria::wire::ByteSpan(chunk_encoded.data(), 1),
            chunk_payload),
        "Chunk decoder should reject truncated payload.");

    novaria::sim::command::CollectResourcePayload collect_payload{};
    const novaria::wire::ByteBuffer collect_encoded =
        novaria::sim::command::EncodeCollectResourcePayload({
            .resource_id = novaria::sim::command::kResourceWood,
            .amount = 5,
        });
    passed &= Expect(
        novaria::sim::command::TryDecodeCollectResourcePayload(
            novaria::wire::ByteSpan(collect_encoded.data(), collect_encoded.size()),
            collect_payload),
        "Collect decoder should accept valid payload.");
    passed &= Expect(
        collect_payload.resource_id == novaria::sim::command::kResourceWood &&
            collect_payload.amount == 5,
        "Collect decoder should parse resource and amount.");
    const novaria::wire::ByteBuffer collect_zero_amount =
        novaria::sim::command::EncodeCollectResourcePayload({
            .resource_id = novaria::sim::command::kResourceWood,
            .amount = 0,
        });
    passed &= Expect(
        !novaria::sim::command::TryDecodeCollectResourcePayload(
            novaria::wire::ByteSpan(collect_zero_amount.data(), collect_zero_amount.size()),
            collect_payload),
        "Collect decoder should reject zero amount.");

    novaria::sim::command::SpawnDropPayload spawn_drop_payload{};
    const novaria::wire::ByteBuffer spawn_drop_encoded =
        novaria::sim::command::EncodeSpawnDropPayload({.tile_x = 4, .tile_y = -6, .material_id = 2, .amount = 3});
    passed &= Expect(
        novaria::sim::command::TryDecodeSpawnDropPayload(
            novaria::wire::ByteSpan(spawn_drop_encoded.data(), spawn_drop_encoded.size()),
            spawn_drop_payload),
        "Spawn drop decoder should accept valid payload.");
    passed &= Expect(
        spawn_drop_payload.tile_x == 4 &&
            spawn_drop_payload.tile_y == -6 &&
            spawn_drop_payload.material_id == 2 &&
            spawn_drop_payload.amount == 3,
        "Spawn drop decoder should parse all fields.");
    const novaria::wire::ByteBuffer spawn_drop_zero_amount =
        novaria::sim::command::EncodeSpawnDropPayload({.tile_x = 4, .tile_y = -6, .material_id = 2, .amount = 0});
    passed &= Expect(
        !novaria::sim::command::TryDecodeSpawnDropPayload(
            novaria::wire::ByteSpan(spawn_drop_zero_amount.data(), spawn_drop_zero_amount.size()),
            spawn_drop_payload),
        "Spawn drop decoder should reject zero amount.");

    novaria::sim::command::PickupProbePayload pickup_probe_payload{};
    const novaria::wire::ByteBuffer pickup_probe_encoded =
        novaria::sim::command::EncodePickupProbePayload({.tile_x = 9, .tile_y = -3});
    passed &= Expect(
        novaria::sim::command::TryDecodePickupProbePayload(
            novaria::wire::ByteSpan(pickup_probe_encoded.data(), pickup_probe_encoded.size()),
            pickup_probe_payload),
        "Pickup probe decoder should accept valid payload.");
    passed &= Expect(
        pickup_probe_payload.tile_x == 9 &&
            pickup_probe_payload.tile_y == -3,
        "Pickup probe decoder should parse both coordinates.");

    novaria::sim::command::InteractionPayload interaction_payload{};
    const novaria::wire::ByteBuffer interaction_encoded =
        novaria::sim::command::EncodeInteractionPayload({
            .interaction_type = novaria::sim::command::kInteractionTypeOpenCrafting,
            .target_tile_x = 2,
            .target_tile_y = -1,
            .target_material_id = 9,
            .result_code = novaria::sim::command::kInteractionResultSuccess,
        });
    passed &= Expect(
        novaria::sim::command::TryDecodeInteractionPayload(
            novaria::wire::ByteSpan(interaction_encoded.data(), interaction_encoded.size()),
            interaction_payload),
        "Interaction decoder should accept valid payload.");
    passed &= Expect(
        interaction_payload.interaction_type ==
            novaria::sim::command::kInteractionTypeOpenCrafting &&
            interaction_payload.target_tile_x == 2 &&
            interaction_payload.target_tile_y == -1 &&
            interaction_payload.target_material_id == 9 &&
            interaction_payload.result_code ==
                novaria::sim::command::kInteractionResultSuccess,
        "Interaction decoder should parse all fields.");

    novaria::sim::command::FireProjectilePayload fire_projectile_payload{};
    const novaria::wire::ByteBuffer fire_projectile_encoded =
        novaria::sim::command::EncodeFireProjectilePayload({
            .origin_tile_x = 1,
            .origin_tile_y = -4,
            .velocity_milli_x = 4500,
            .velocity_milli_y = 0,
            .damage = 13,
            .lifetime_ticks = 180,
            .faction = 1,
        });
    passed &= Expect(
        novaria::sim::command::TryDecodeFireProjectilePayload(
            novaria::wire::ByteSpan(fire_projectile_encoded.data(), fire_projectile_encoded.size()),
            fire_projectile_payload),
        "Fire projectile decoder should accept valid payload.");
    passed &= Expect(
        fire_projectile_payload.origin_tile_x == 1 &&
            fire_projectile_payload.origin_tile_y == -4 &&
            fire_projectile_payload.velocity_milli_x == 4500 &&
            fire_projectile_payload.velocity_milli_y == 0 &&
            fire_projectile_payload.damage == 13 &&
            fire_projectile_payload.lifetime_ticks == 180 &&
            fire_projectile_payload.faction == 1,
        "Fire projectile decoder should parse all fields.");
    const novaria::wire::ByteBuffer fire_projectile_zero_lifetime =
        novaria::sim::command::EncodeFireProjectilePayload({
            .origin_tile_x = 1,
            .origin_tile_y = -4,
            .velocity_milli_x = 4500,
            .velocity_milli_y = 0,
            .damage = 13,
            .lifetime_ticks = 0,
            .faction = 1,
        });
    passed &= Expect(
        !novaria::sim::command::TryDecodeFireProjectilePayload(
            novaria::wire::ByteSpan(fire_projectile_zero_lifetime.data(), fire_projectile_zero_lifetime.size()),
            fire_projectile_payload),
        "Fire projectile decoder should reject zero lifetime.");

    novaria::sim::TypedPlayerCommand typed_command{};
    passed &= Expect(
        novaria::sim::TryDecodePlayerCommand(
            novaria::net::PlayerCommand{
                .player_id = 1,
                .command_id = novaria::sim::command::kCombatFireProjectile,
                .payload = fire_projectile_encoded,
            },
            typed_command) &&
            typed_command.type == novaria::sim::TypedPlayerCommandType::CombatFireProjectile,
        "Typed command bridge should decode projectile command.");
    passed &= Expect(
        !novaria::sim::TryDecodePlayerCommand(
            novaria::net::PlayerCommand{
                .player_id = 1,
                .command_id = novaria::sim::command::kCombatFireProjectile,
                .payload = {0x01, 0x02},
            },
            typed_command),
        "Typed command bridge should reject invalid projectile payload.");
    passed &= Expect(
        novaria::sim::TryDecodePlayerCommand(
            novaria::net::PlayerCommand{
                .player_id = 1,
                .command_id = novaria::sim::command::kGameplayInteraction,
                .payload = interaction_encoded,
            },
            typed_command) &&
            typed_command.type == novaria::sim::TypedPlayerCommandType::GameplayInteraction,
        "Typed command bridge should decode gameplay interaction command.");

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
                novaria::wire::ByteSpan(
                    net.published_snapshot_payloads[0][0].data(),
                    net.published_snapshot_payloads[0][0].size()),
                decoded_snapshot,
                decode_error),
            "Encoded chunk payload should be decodable.");
    }

    kernel.SubmitLocalCommand({
        .player_id = 12,
        .command_id = novaria::sim::command::kJump,
        .payload = {},
    });
    kernel.SubmitLocalCommand({
        .player_id = 12,
        .command_id = novaria::sim::command::kAttack,
        .payload = {},
    });
    kernel.Update(1.0 / 60.0);
    passed &= Expect(net.submitted_commands.size() == 2, "Submitted commands should be forwarded on update.");
    if (net.submitted_commands.size() == 2) {
        passed &= Expect(
            net.submitted_commands[0].command_id == novaria::sim::command::kJump,
            "First command type should match.");
        passed &= Expect(
            net.submitted_commands[1].command_id == novaria::sim::command::kAttack,
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
    novaria::net::NetServiceUdpPeer net;
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

    kernel.SubmitLocalCommand({.player_id = 3, .command_id = 999, .payload = {0x01}});
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
            .command_id = novaria::sim::command::kJump,
            .payload = {},
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
        .command_id = novaria::sim::command::kJump,
        .payload = {},
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
        !kernel.ApplyRemoteChunkPayload(novaria::wire::ByteSpan(), error),
        "ApplyRemoteChunkPayload should fail before initialize.");

    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    passed &= Expect(
        !kernel.ApplyRemoteChunkPayload(novaria::wire::ByteSpan(reinterpret_cast<const novaria::wire::Byte*>("\x01"), 1), error),
        "ApplyRemoteChunkPayload should fail for invalid payload.");

    novaria::world::ChunkSnapshot snapshot{
        .chunk_coord = {.x = 2, .y = -3},
        .tiles = {1, 2, 3, 4},
    };
    novaria::wire::ByteBuffer payload;
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(snapshot, payload, error),
        "EncodeChunkSnapshot should succeed.");
    passed &= Expect(
        kernel.ApplyRemoteChunkPayload(novaria::wire::ByteSpan(payload.data(), payload.size()), error),
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
    kernel.SetAuthorityMode(novaria::sim::SimulationAuthorityMode::Replica);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    novaria::world::ChunkSnapshot snapshot{
        .chunk_coord = {.x = -4, .y = 9},
        .tiles = {11, 12, 13, 14},
    };
    novaria::wire::ByteBuffer payload;
    passed &= Expect(
        novaria::world::WorldSnapshotCodec::EncodeChunkSnapshot(snapshot, payload, error),
        "EncodeChunkSnapshot should succeed.");
    net.pending_remote_chunk_payloads.push_back(payload);
    net.pending_remote_chunk_payloads.push_back({0x01, 0x02});

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
    novaria::wire::ByteBuffer remote_payload;
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

bool TestAuthorityPublishesLoadedChunksAfterConnectionEstablished() {
    bool passed = true;

    FakeWorldService world;
    world.loaded_chunks = {{.x = 9, .y = -3}};
    world.available_snapshots = {
        {.chunk_coord = {.x = 9, .y = -3}, .tiles = {1, 2, 3, 4}},
    };
    FakeNetService net;
    net.auto_progress_connection = false;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        net.published_snapshots.empty(),
        "Connecting session should not publish loaded chunk snapshots.");

    net.session_state = novaria::net::NetSessionState::Connected;
    net.last_transition_reason = "test_connected";
    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        net.published_snapshots.size() == 1,
        "Connection transition should trigger initial loaded chunk snapshot publish.");
    if (net.published_snapshots.size() == 1) {
        passed &= Expect(
            net.published_snapshots[0].second == 1,
            "Initial sync publish should include loaded chunk snapshot.");
    }

    kernel.Shutdown();
    return passed;
}

bool TestDirtyChunksRetainedUntilConnectionEstablished() {
    bool passed = true;

    FakeWorldService world;
    world.dirty_batches = {
        {{.x = 3, .y = 4}},
    };
    world.available_snapshots = {
        {.chunk_coord = {.x = 3, .y = 4}, .tiles = {8, 8, 8, 8}},
    };
    FakeNetService net;
    net.auto_progress_connection = false;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");
    kernel.Update(1.0 / 60.0);
    passed &= Expect(
        net.published_snapshots.empty(),
        "Dirty chunks should not publish while net session is not connected.");

    net.session_state = novaria::net::NetSessionState::Connected;
    net.last_transition_reason = "test_connected";
    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        net.published_snapshots.size() == 1,
        "Previously queued dirty chunk should publish after connection established.");
    if (net.published_snapshots.size() == 1) {
        passed &= Expect(
            net.published_snapshots[0].second == 1,
            "Retained dirty publish should contain one chunk.");
    }

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
        .command_id = novaria::sim::command::kWorldLoadChunk,
        .payload = novaria::sim::command::EncodeWorldChunkPayload({.chunk_x = 2, .chunk_y = -1}),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kWorldSetTile,
        .payload = novaria::sim::command::EncodeWorldSetTilePayload({.tile_x = 10, .tile_y = 11, .material_id = 7}),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kWorldUnloadChunk,
        .payload = novaria::sim::command::EncodeWorldChunkPayload({.chunk_x = 2, .chunk_y = -1}),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kWorldSetTile,
        .payload = {0x01, 0x02},
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

bool TestReplicaModeRejectsLocalWorldWrites() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    novaria::sim::SimulationKernel kernel(world, net, script);
    kernel.SetAuthorityMode(novaria::sim::SimulationAuthorityMode::Replica);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kWorldLoadChunk,
        .payload = novaria::sim::command::EncodeWorldChunkPayload({.chunk_x = 4, .chunk_y = 5}),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kWorldSetTile,
        .payload = novaria::sim::command::EncodeWorldSetTilePayload({.tile_x = 10, .tile_y = 11, .material_id = 7}),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kWorldUnloadChunk,
        .payload = novaria::sim::command::EncodeWorldChunkPayload({.chunk_x = 4, .chunk_y = 5}),
    });
    kernel.Update(1.0 / 60.0);

    passed &= Expect(
        net.submitted_commands.size() == 3,
        "Replica mode should still forward local commands to net service.");
    passed &= Expect(
        world.loaded_chunks.empty() &&
            world.unloaded_chunks.empty() &&
            world.applied_tile_mutations.empty(),
        "Replica mode should block local world writes.");

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

    // Allow workbench-gated crafting in sim.
    world.SetTile(1, -2, novaria::world::material::kWorkbench);

    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kGameplayCollectResource,
        .payload = novaria::sim::command::EncodeCollectResourcePayload({
            .resource_id = novaria::sim::command::kResourceWood,
            .amount = 20,
        }),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kGameplayCollectResource,
        .payload = novaria::sim::command::EncodeCollectResourcePayload({
            .resource_id = novaria::sim::command::kResourceStone,
            .amount = 20,
        }),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kGameplayCraftRecipe,
        .payload = novaria::sim::command::EncodeCraftRecipePayload({
            .recipe_index = 0,
        }),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kGameplayCraftRecipe,
        .payload = novaria::sim::command::EncodeCraftRecipePayload({
            .recipe_index = 1,
        }),
    });
    for (int index = 0; index < 3; ++index) {
        kernel.SubmitLocalCommand({
            .player_id = 1,
            .command_id = novaria::sim::command::kGameplayAttackEnemy,
            .payload = {},
        });
    }
    for (int index = 0; index < 6; ++index) {
        kernel.SubmitLocalCommand({
            .player_id = 1,
            .command_id = novaria::sim::command::kGameplayAttackBoss,
            .payload = {},
        });
    }

    kernel.Update(1.0 / 60.0);
    const novaria::sim::GameplayProgressSnapshot progress = kernel.GameplayProgress();

    passed &= Expect(
        progress.wood_collected == 20 && progress.stone_collected == 20,
        "Gameplay resources should track collected totals.");
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

bool TestGameplayDropPickupAndInteractionDispatchScriptEvents() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.auto_progress_connection = false;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    kernel.SubmitLocalCommand({
        .player_id = 7,
        .command_id = novaria::sim::command::kGameplaySpawnDrop,
        .payload = novaria::sim::command::EncodeSpawnDropPayload({
            .tile_x = 2,
            .tile_y = -3,
            .material_id = 2,
            .amount = 1,
        }),
    });
    kernel.SubmitLocalCommand({
        .player_id = 7,
        .command_id = novaria::sim::command::kGameplayPickupProbe,
        .payload = novaria::sim::command::EncodePickupProbePayload({.tile_x = 2, .tile_y = -3}),
    });
    kernel.SubmitLocalCommand({
        .player_id = 7,
        .command_id = novaria::sim::command::kGameplayInteraction,
        .payload = novaria::sim::command::EncodeInteractionPayload({
            .interaction_type = novaria::sim::command::kInteractionTypeOpenCrafting,
            .target_tile_x = 2,
            .target_tile_y = -3,
            .target_material_id = 9,
            .result_code = novaria::sim::command::kInteractionResultSuccess,
        }),
    });
    kernel.Update(1.0 / 60.0);

    const std::vector<novaria::sim::GameplayPickupEvent> pickup_events =
        kernel.ConsumePickupEventsForPlayer(7);
    passed &= Expect(
        pickup_events.size() == 1,
        "Drop spawn + pickup probe should resolve one pickup event.");
    if (pickup_events.size() == 1) {
        passed &= Expect(
            pickup_events[0].material_id == 2 &&
                pickup_events[0].amount == 1 &&
                pickup_events[0].tile_x == 2 &&
                pickup_events[0].tile_y == -3,
            "Resolved pickup event should match drop payload.");
    }

    const std::vector<novaria::sim::GameplayPickupEvent> drained_events =
        kernel.ConsumePickupEventsForPlayer(7);
    passed &= Expect(
        drained_events.empty(),
        "Pickup event queue should be consumable exactly once per player.");

    bool saw_pickup_event = false;
    bool saw_interaction_event = false;
    for (const auto& event : script.dispatched_events) {
        if (event.event_name == "gameplay.pickup") {
            saw_pickup_event = true;
            passed &= Expect(
                event.payload.find("player=7") != std::string::npos &&
                    event.payload.find("material_id=2") != std::string::npos &&
                    event.payload.find("amount=1") != std::string::npos,
                "Gameplay pickup event payload should include pickup fields.");
        }
        if (event.event_name == "gameplay.interaction") {
            saw_interaction_event = true;
            passed &= Expect(
                event.payload.find("type=open_crafting") != std::string::npos &&
                    event.payload.find("result=success") != std::string::npos &&
                    event.payload.find("branch=open_crafting") != std::string::npos,
                "Gameplay interaction event payload should include branch fields.");
        }
    }
    passed &= Expect(
        saw_pickup_event,
        "Gameplay pickup should dispatch script event.");
    passed &= Expect(
        saw_interaction_event,
        "Gameplay interaction should dispatch script event.");
    passed &= Expect(
        net.submitted_commands.size() == 3,
        "Gameplay drop/pickup/interaction commands should enter net stream.");

    kernel.Shutdown();
    return passed;
}

bool TestPlaceRejectedWhenTileOverlapsPlayerCollider() {
    bool passed = true;

    FakeWorldService world;
    FakeNetService net;
    FakeScriptHost script;
    net.auto_progress_connection = false;
    novaria::sim::SimulationKernel kernel(world, net, script);

    std::string error;
    passed &= Expect(kernel.Initialize(error), "Kernel initialize should succeed.");

    constexpr int kGroundY = 10;
    for (int x = -16; x <= 16; ++x) {
        for (int y = kGroundY; y <= kGroundY + 32; ++y) {
            world.SetTile(x, y, novaria::world::material::kStone);
        }
    }

    bool settled = false;
    for (int tick = 0; tick < 360; ++tick) {
        kernel.Update(1.0 / 60.0);
        const novaria::sim::PlayerMotionSnapshot motion = kernel.LocalPlayerMotion();
        if (motion.on_ground && std::fabs(motion.position_y - static_cast<float>(kGroundY)) <= 0.05F) {
            settled = true;
            break;
        }
    }
    passed &= Expect(settled, "Player should settle on the configured ground before placement test.");

    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kGameplaySpawnDrop,
        .payload = novaria::sim::command::EncodeSpawnDropPayload({
            .tile_x = 0,
            .tile_y = kGroundY - 1,
            .material_id = novaria::world::material::kDirt,
            .amount = 1,
        }),
    });
    kernel.SubmitLocalCommand({
        .player_id = 1,
        .command_id = novaria::sim::command::kGameplayPickupProbe,
        .payload = novaria::sim::command::EncodePickupProbePayload({.tile_x = 0, .tile_y = kGroundY - 1}),
    });
    kernel.Update(1.0 / 60.0);

    const novaria::sim::PlayerInventorySnapshot inventory_after_pickup = kernel.InventorySnapshot(1);
    passed &= Expect(
        inventory_after_pickup.dirt_count >= 1,
        "Pickup should grant at least 1 dirt for placement test.");

    const novaria::sim::PlayerMotionSnapshot motion_before_place = kernel.LocalPlayerMotion();
    const int player_tile_x = static_cast<int>(std::floor(motion_before_place.position_x));
    const int player_tile_y = static_cast<int>(std::floor(motion_before_place.position_y));
    const int target_tile_x = player_tile_x;
    const int target_tile_y = player_tile_y - 1;

    const std::size_t mutation_count_before = world.applied_tile_mutations.size();
    for (int tick = 0; tick < 8; ++tick) {
        kernel.SubmitLocalCommand({
            .player_id = 1,
            .command_id = novaria::sim::command::kGameplayActionPrimary,
            .payload = novaria::sim::command::EncodeActionPrimaryPayload({
                .target_tile_x = target_tile_x,
                .target_tile_y = target_tile_y,
                .hotbar_row = 0,
                .hotbar_slot = 2,
            }),
        });
        kernel.Update(1.0 / 60.0);
    }

    passed &= Expect(
        world.applied_tile_mutations.size() == mutation_count_before,
        "Placing a solid tile overlapping the player should be rejected (no world mutation).");

    const novaria::sim::PlayerInventorySnapshot inventory_after_place = kernel.InventorySnapshot(1);
    passed &= Expect(
        inventory_after_place.dirt_count == inventory_after_pickup.dirt_count,
        "Rejected placement should not consume inventory.");

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
    passed &= TestReplicaModeRejectsLocalWorldWrites();
    passed &= TestGameplayLoopCommandsReachBossDefeat();
    passed &= TestGameplayDropPickupAndInteractionDispatchScriptEvents();
    passed &= TestPlaceRejectedWhenTileOverlapsPlayerCollider();
    passed &= TestUpdateConsumesRemoteChunkPayloads();
    passed &= TestUpdateSkipsNetExchangeWhenSessionNotConnected();
    passed &= TestAuthorityPublishesLoadedChunksAfterConnectionEstablished();
    passed &= TestDirtyChunksRetainedUntilConnectionEstablished();

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_simulation_kernel_tests\n";
    return 0;
}
