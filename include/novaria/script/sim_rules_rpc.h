#pragma once

#include "script/sim_rules_schema.h"
#include "wire/byte_io.h"

#include <cstdint>

namespace novaria::script::simrpc {

inline wire::ByteBuffer EncodeValidateRequest() {
    wire::ByteWriter writer;
    writer.WriteU8(kVersion);
    writer.WriteU8(static_cast<wire::Byte>(Command::Validate));
    return writer.TakeBuffer();
}

inline bool TryDecodeValidateRequest(wire::ByteSpan payload) {
    wire::ByteReader reader(payload);
    wire::Byte version = 0;
    wire::Byte command = 0;
    if (!reader.ReadU8(version) ||
        !reader.ReadU8(command) ||
        !reader.IsFullyConsumed()) {
        return false;
    }
    return version == kVersion && command == static_cast<wire::Byte>(Command::Validate);
}

inline bool TryDecodeValidateResponse(wire::ByteSpan payload, ValidateResponse& out_response) {
    wire::ByteReader reader(payload);
    wire::Byte version = 0;
    wire::Byte command = 0;
    wire::Byte ok = 0;
    if (!reader.ReadU8(version) ||
        !reader.ReadU8(command) ||
        !reader.ReadU8(ok) ||
        !reader.IsFullyConsumed()) {
        return false;
    }
    if (version != kVersion || command != static_cast<wire::Byte>(Command::Validate)) {
        return false;
    }
    out_response = ValidateResponse{.ok = ok != 0};
    return true;
}

inline wire::ByteBuffer EncodeActionPrimaryRequest(const ActionPrimaryRequest& request) {
    wire::ByteWriter writer;
    writer.WriteU8(kVersion);
    writer.WriteU8(static_cast<wire::Byte>(Command::GameplayActionPrimary));
    writer.WriteVarUInt(request.player_id);
    writer.WriteVarInt(request.player_tile_x);
    writer.WriteVarInt(request.player_tile_y);
    writer.WriteVarInt(request.target_tile_x);
    writer.WriteVarInt(request.target_tile_y);
    writer.WriteU8(request.hotbar_row);
    writer.WriteU8(request.hotbar_slot);
    writer.WriteVarUInt(request.dirt_count);
    writer.WriteVarUInt(request.stone_count);
    writer.WriteVarUInt(request.wood_count);
    writer.WriteVarUInt(request.coal_count);
    writer.WriteVarUInt(request.torch_count);
    writer.WriteVarUInt(request.workbench_count);
    writer.WriteVarUInt(request.wood_sword_count);
    writer.WriteU8(request.has_pickaxe_tool ? 1 : 0);
    writer.WriteU8(request.has_axe_tool ? 1 : 0);
    writer.WriteU8(request.target_is_air ? 1 : 0);
    writer.WriteVarUInt(request.harvest_ticks);
    wire::Byte harvest_flags = 0;
    if (request.harvestable_by_pickaxe) {
        harvest_flags |= 1;
    }
    if (request.harvestable_by_axe) {
        harvest_flags |= 2;
    }
    if (request.harvestable_by_sword) {
        harvest_flags |= 4;
    }
    writer.WriteU8(harvest_flags);
    return writer.TakeBuffer();
}

inline bool TryDecodeActionPrimaryRequest(
    wire::ByteSpan payload,
    ActionPrimaryRequest& out_request) {
    wire::ByteReader reader(payload);
    wire::Byte version = 0;
    wire::Byte command = 0;
    std::uint64_t player_id = 0;
    std::int64_t player_tile_x = 0;
    std::int64_t player_tile_y = 0;
    std::int64_t target_tile_x = 0;
    std::int64_t target_tile_y = 0;
    wire::Byte hotbar_row = 0;
    wire::Byte hotbar_slot = 0;
    std::uint64_t dirt_count = 0;
    std::uint64_t stone_count = 0;
    std::uint64_t wood_count = 0;
    std::uint64_t coal_count = 0;
    std::uint64_t torch_count = 0;
    std::uint64_t workbench_count = 0;
    std::uint64_t wood_sword_count = 0;
    wire::Byte has_pickaxe = 0;
    wire::Byte has_axe = 0;
    wire::Byte target_is_air = 0;
    std::uint64_t harvest_ticks = 0;
    wire::Byte harvest_flags = 0;
    if (!reader.ReadU8(version) ||
        !reader.ReadU8(command) ||
        !reader.ReadVarUInt(player_id) ||
        !reader.ReadVarInt(player_tile_x) ||
        !reader.ReadVarInt(player_tile_y) ||
        !reader.ReadVarInt(target_tile_x) ||
        !reader.ReadVarInt(target_tile_y) ||
        !reader.ReadU8(hotbar_row) ||
        !reader.ReadU8(hotbar_slot) ||
        !reader.ReadVarUInt(dirt_count) ||
        !reader.ReadVarUInt(stone_count) ||
        !reader.ReadVarUInt(wood_count) ||
        !reader.ReadVarUInt(coal_count) ||
        !reader.ReadVarUInt(torch_count) ||
        !reader.ReadVarUInt(workbench_count) ||
        !reader.ReadVarUInt(wood_sword_count) ||
        !reader.ReadU8(has_pickaxe) ||
        !reader.ReadU8(has_axe) ||
        !reader.ReadU8(target_is_air) ||
        !reader.ReadVarUInt(harvest_ticks) ||
        !reader.ReadU8(harvest_flags) ||
        !reader.IsFullyConsumed()) {
        return false;
    }
    if (version != kVersion || command != static_cast<wire::Byte>(Command::GameplayActionPrimary)) {
        return false;
    }
    if (player_id > UINT32_MAX ||
        dirt_count > UINT32_MAX ||
        stone_count > UINT32_MAX ||
        wood_count > UINT32_MAX ||
        coal_count > UINT32_MAX ||
        torch_count > UINT32_MAX ||
        workbench_count > UINT32_MAX ||
        wood_sword_count > UINT32_MAX ||
        harvest_ticks > UINT32_MAX) {
        return false;
    }
    if ((harvest_flags & ~static_cast<wire::Byte>(0x07)) != 0) {
        return false;
    }
    out_request = ActionPrimaryRequest{
        .player_id = static_cast<std::uint32_t>(player_id),
        .player_tile_x = static_cast<int>(player_tile_x),
        .player_tile_y = static_cast<int>(player_tile_y),
        .target_tile_x = static_cast<int>(target_tile_x),
        .target_tile_y = static_cast<int>(target_tile_y),
        .hotbar_row = hotbar_row,
        .hotbar_slot = hotbar_slot,
        .dirt_count = static_cast<std::uint32_t>(dirt_count),
        .stone_count = static_cast<std::uint32_t>(stone_count),
        .wood_count = static_cast<std::uint32_t>(wood_count),
        .coal_count = static_cast<std::uint32_t>(coal_count),
        .torch_count = static_cast<std::uint32_t>(torch_count),
        .workbench_count = static_cast<std::uint32_t>(workbench_count),
        .wood_sword_count = static_cast<std::uint32_t>(wood_sword_count),
        .has_pickaxe_tool = has_pickaxe != 0,
        .has_axe_tool = has_axe != 0,
        .target_is_air = target_is_air != 0,
        .harvest_ticks = static_cast<std::uint32_t>(harvest_ticks),
        .harvestable_by_pickaxe = (harvest_flags & 1) != 0,
        .harvestable_by_axe = (harvest_flags & 2) != 0,
        .harvestable_by_sword = (harvest_flags & 4) != 0,
    };
    return true;
}

inline bool TryDecodeActionPrimaryResponse(
    wire::ByteSpan payload,
    ActionPrimaryResponse& out_response) {
    wire::ByteReader reader(payload);
    wire::Byte version = 0;
    wire::Byte command = 0;
    wire::Byte result = 0;
    wire::Byte place_kind = 0;
    std::uint64_t required_ticks = 0;
    if (!reader.ReadU8(version) ||
        !reader.ReadU8(command) ||
        !reader.ReadU8(result) ||
        !reader.ReadU8(place_kind) ||
        !reader.ReadVarUInt(required_ticks) ||
        !reader.IsFullyConsumed()) {
        return false;
    }
    if (version != kVersion || command != static_cast<wire::Byte>(Command::GameplayActionPrimary)) {
        return false;
    }
    if (result > static_cast<wire::Byte>(ActionPrimaryResult::Place) ||
        place_kind > static_cast<wire::Byte>(PlaceKind::Workbench) ||
        required_ticks > UINT32_MAX) {
        return false;
    }
    out_response = ActionPrimaryResponse{
        .result = static_cast<ActionPrimaryResult>(result),
        .place_kind = static_cast<PlaceKind>(place_kind),
        .required_ticks = static_cast<std::uint32_t>(required_ticks),
    };
    return true;
}

inline wire::ByteBuffer EncodeCraftRecipeRequest(const CraftRecipeRequest& request) {
    wire::ByteWriter writer;
    writer.WriteU8(kVersion);
    writer.WriteU8(static_cast<wire::Byte>(Command::GameplayCraftRecipe));
    writer.WriteVarUInt(request.player_id);
    writer.WriteVarInt(request.player_tile_x);
    writer.WriteVarInt(request.player_tile_y);
    writer.WriteU8(request.recipe_index);
    writer.WriteU8(request.workbench_reachable ? 1 : 0);
    writer.WriteVarUInt(request.dirt_count);
    writer.WriteVarUInt(request.stone_count);
    writer.WriteVarUInt(request.wood_count);
    writer.WriteVarUInt(request.coal_count);
    writer.WriteVarUInt(request.torch_count);
    writer.WriteVarUInt(request.workbench_count);
    writer.WriteVarUInt(request.wood_sword_count);
    return writer.TakeBuffer();
}

inline bool TryDecodeCraftRecipeRequest(
    wire::ByteSpan payload,
    CraftRecipeRequest& out_request) {
    wire::ByteReader reader(payload);
    wire::Byte version = 0;
    wire::Byte command = 0;
    std::uint64_t player_id = 0;
    std::int64_t player_tile_x = 0;
    std::int64_t player_tile_y = 0;
    wire::Byte recipe_index = 0;
    wire::Byte workbench_reachable = 0;
    std::uint64_t dirt_count = 0;
    std::uint64_t stone_count = 0;
    std::uint64_t wood_count = 0;
    std::uint64_t coal_count = 0;
    std::uint64_t torch_count = 0;
    std::uint64_t workbench_count = 0;
    std::uint64_t wood_sword_count = 0;
    if (!reader.ReadU8(version) ||
        !reader.ReadU8(command) ||
        !reader.ReadVarUInt(player_id) ||
        !reader.ReadVarInt(player_tile_x) ||
        !reader.ReadVarInt(player_tile_y) ||
        !reader.ReadU8(recipe_index) ||
        !reader.ReadU8(workbench_reachable) ||
        !reader.ReadVarUInt(dirt_count) ||
        !reader.ReadVarUInt(stone_count) ||
        !reader.ReadVarUInt(wood_count) ||
        !reader.ReadVarUInt(coal_count) ||
        !reader.ReadVarUInt(torch_count) ||
        !reader.ReadVarUInt(workbench_count) ||
        !reader.ReadVarUInt(wood_sword_count) ||
        !reader.IsFullyConsumed()) {
        return false;
    }
    if (version != kVersion || command != static_cast<wire::Byte>(Command::GameplayCraftRecipe)) {
        return false;
    }
    if (player_id > UINT32_MAX ||
        dirt_count > UINT32_MAX ||
        stone_count > UINT32_MAX ||
        wood_count > UINT32_MAX ||
        coal_count > UINT32_MAX ||
        torch_count > UINT32_MAX ||
        workbench_count > UINT32_MAX ||
        wood_sword_count > UINT32_MAX) {
        return false;
    }
    out_request = CraftRecipeRequest{
        .player_id = static_cast<std::uint32_t>(player_id),
        .player_tile_x = static_cast<int>(player_tile_x),
        .player_tile_y = static_cast<int>(player_tile_y),
        .recipe_index = recipe_index,
        .workbench_reachable = workbench_reachable != 0,
        .dirt_count = static_cast<std::uint32_t>(dirt_count),
        .stone_count = static_cast<std::uint32_t>(stone_count),
        .wood_count = static_cast<std::uint32_t>(wood_count),
        .coal_count = static_cast<std::uint32_t>(coal_count),
        .torch_count = static_cast<std::uint32_t>(torch_count),
        .workbench_count = static_cast<std::uint32_t>(workbench_count),
        .wood_sword_count = static_cast<std::uint32_t>(wood_sword_count),
    };
    return true;
}

inline bool TryDecodeCraftRecipeResponse(
    wire::ByteSpan payload,
    CraftRecipeResponse& out_response) {
    wire::ByteReader reader(payload);
    wire::Byte version = 0;
    wire::Byte command = 0;
    wire::Byte result = 0;
    std::int64_t dirt_delta = 0;
    std::int64_t stone_delta = 0;
    std::int64_t wood_delta = 0;
    std::int64_t coal_delta = 0;
    std::int64_t torch_delta = 0;
    std::int64_t workbench_delta = 0;
    std::int64_t wood_sword_delta = 0;
    wire::Byte crafted_kind = 0;
    wire::Byte milestone_flags = 0;
    if (!reader.ReadU8(version) ||
        !reader.ReadU8(command) ||
        !reader.ReadU8(result) ||
        !reader.ReadVarInt(dirt_delta) ||
        !reader.ReadVarInt(stone_delta) ||
        !reader.ReadVarInt(wood_delta) ||
        !reader.ReadVarInt(coal_delta) ||
        !reader.ReadVarInt(torch_delta) ||
        !reader.ReadVarInt(workbench_delta) ||
        !reader.ReadVarInt(wood_sword_delta) ||
        !reader.ReadU8(crafted_kind) ||
        !reader.ReadU8(milestone_flags) ||
        !reader.IsFullyConsumed()) {
        return false;
    }
    if (version != kVersion || command != static_cast<wire::Byte>(Command::GameplayCraftRecipe)) {
        return false;
    }
    if (result > static_cast<wire::Byte>(CraftRecipeResult::Craft) ||
        crafted_kind > static_cast<wire::Byte>(CraftedKind::Torch)) {
        return false;
    }

    out_response = CraftRecipeResponse{
        .result = static_cast<CraftRecipeResult>(result),
        .dirt_delta = static_cast<int>(dirt_delta),
        .stone_delta = static_cast<int>(stone_delta),
        .wood_delta = static_cast<int>(wood_delta),
        .coal_delta = static_cast<int>(coal_delta),
        .torch_delta = static_cast<int>(torch_delta),
        .workbench_delta = static_cast<int>(workbench_delta),
        .wood_sword_delta = static_cast<int>(wood_sword_delta),
        .crafted_kind = static_cast<CraftedKind>(crafted_kind),
        .mark_workbench_built = (milestone_flags & 1) != 0,
        .mark_sword_crafted = (milestone_flags & 2) != 0,
    };
    return true;
}

inline wire::ByteBuffer EncodeValidateResponse(bool ok) {
    wire::ByteWriter writer;
    writer.WriteU8(kVersion);
    writer.WriteU8(static_cast<wire::Byte>(Command::Validate));
    writer.WriteU8(ok ? 1 : 0);
    return writer.TakeBuffer();
}

inline wire::ByteBuffer EncodeActionPrimaryResponse(
    ActionPrimaryResult result,
    PlaceKind place_kind,
    std::uint32_t required_ticks) {
    wire::ByteWriter writer;
    writer.WriteU8(kVersion);
    writer.WriteU8(static_cast<wire::Byte>(Command::GameplayActionPrimary));
    writer.WriteU8(static_cast<wire::Byte>(result));
    writer.WriteU8(static_cast<wire::Byte>(place_kind));
    writer.WriteVarUInt(required_ticks);
    return writer.TakeBuffer();
}

inline wire::ByteBuffer EncodeCraftRecipeResponse(const CraftRecipeResponse& response) {
    wire::ByteWriter writer;
    writer.WriteU8(kVersion);
    writer.WriteU8(static_cast<wire::Byte>(Command::GameplayCraftRecipe));
    writer.WriteU8(static_cast<wire::Byte>(response.result));
    writer.WriteVarInt(response.dirt_delta);
    writer.WriteVarInt(response.stone_delta);
    writer.WriteVarInt(response.wood_delta);
    writer.WriteVarInt(response.coal_delta);
    writer.WriteVarInt(response.torch_delta);
    writer.WriteVarInt(response.workbench_delta);
    writer.WriteVarInt(response.wood_sword_delta);
    writer.WriteU8(static_cast<wire::Byte>(response.crafted_kind));
    wire::Byte milestone_flags = 0;
    if (response.mark_workbench_built) {
        milestone_flags |= 1;
    }
    if (response.mark_sword_crafted) {
        milestone_flags |= 2;
    }
    writer.WriteU8(milestone_flags);
    return writer.TakeBuffer();
}

}  // namespace novaria::script::simrpc
