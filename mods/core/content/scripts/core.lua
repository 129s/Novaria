novaria = novaria or {}
novaria.core = novaria.core or {}
novaria.core.last_tick = novaria.core.last_tick or 0
novaria.core.last_delta = novaria.core.last_delta or 0
novaria.core.last_event_name = novaria.core.last_event_name or ""
novaria.core.last_event_payload = novaria.core.last_event_payload or ""
novaria.core.event_count = novaria.core.event_count or 0

function novaria_on_tick(tick_index, delta_seconds)
  novaria.core.last_tick = tick_index
  novaria.core.last_delta = delta_seconds
end

function novaria_on_event(event_name, payload)
  novaria.core.last_event_name = event_name
  novaria.core.last_event_payload = payload
  novaria.core.event_count = (novaria.core.event_count or 0) + 1
end

-- BEGIN NOVARIA_SIMRPC_CONSTANTS (GENERATED)
local RPC_VERSION = 1

local CMD_VALIDATE = 0
local CMD_ACTION_PRIMARY = 1
local CMD_CRAFT_RECIPE = 2

local ACTION_REJECT = 0
local ACTION_HARVEST = 1
local ACTION_PLACE = 2

local PLACE_NONE = 0
local PLACE_DIRT = 1
local PLACE_STONE = 2
local PLACE_TORCH = 3
local PLACE_WORKBENCH = 4

local CRAFT_REJECT = 0
local CRAFT_CRAFT = 1

local CRAFTED_NONE = 0
local CRAFTED_WORKBENCH = 1
local CRAFTED_TORCH = 2
-- END NOVARIA_SIMRPC_CONSTANTS (GENERATED)

local function read_u8(text, offset)
  local value = string.byte(text, offset)
  if value == nil then
    return nil, offset
  end
  return value, offset + 1
end

local function read_varuint(text, offset)
  local result = 0
  local shift = 0
  for _ = 1, 10 do
    local b = string.byte(text, offset)
    if b == nil then
      return nil, offset
    end
    offset = offset + 1
    local chunk = b % 128
    result = result + chunk * (2 ^ shift)
    if b < 128 then
      return result, offset
    end
    shift = shift + 7
  end
  return nil, offset
end

local function read_varint(text, offset)
  local encoded, next_offset = read_varuint(text, offset)
  if encoded == nil then
    return nil, offset
  end
  if (encoded % 2) == 0 then
    return encoded / 2, next_offset
  end
  return -((encoded + 1) / 2), next_offset
end

local function write_u8(out, value)
  out[#out + 1] = string.char(value % 256)
end

local function write_varuint(out, value)
  value = math.floor(value)
  while value >= 128 do
    local byte = (value % 128) + 128
    out[#out + 1] = string.char(byte)
    value = math.floor(value / 128)
  end
  out[#out + 1] = string.char(value)
end

local function write_varint(out, value)
  value = math.floor(value)
  local encoded = 0
  if value >= 0 then
    encoded = value * 2
  else
    encoded = (-value) * 2 - 1
  end
  write_varuint(out, encoded)
end

local function encode_validate_response(ok)
  local out = {}
  write_u8(out, RPC_VERSION)
  write_u8(out, CMD_VALIDATE)
  write_u8(out, ok and 1 or 0)
  return table.concat(out)
end

local function encode_action_primary_response(result, place_kind, required_ticks)
  local out = {}
  write_u8(out, RPC_VERSION)
  write_u8(out, CMD_ACTION_PRIMARY)
  write_u8(out, result)
  write_u8(out, place_kind)
  write_varuint(out, required_ticks or 0)
  return table.concat(out)
end

local function encode_craft_recipe_response(
  result,
  dirt_delta,
  stone_delta,
  wood_delta,
  coal_delta,
  torch_delta,
  workbench_delta,
  wood_sword_delta,
  crafted_kind,
  mark_workbench_built,
  mark_sword_crafted)
  local out = {}
  write_u8(out, RPC_VERSION)
  write_u8(out, CMD_CRAFT_RECIPE)
  write_u8(out, result)
  write_varint(out, dirt_delta or 0)
  write_varint(out, stone_delta or 0)
  write_varint(out, wood_delta or 0)
  write_varint(out, coal_delta or 0)
  write_varint(out, torch_delta or 0)
  write_varint(out, workbench_delta or 0)
  write_varint(out, wood_sword_delta or 0)
  write_u8(out, crafted_kind or CRAFTED_NONE)
  local milestone_flags = 0
  if mark_workbench_built then
    milestone_flags = milestone_flags + 1
  end
  if mark_sword_crafted then
    milestone_flags = milestone_flags + 2
  end
  write_u8(out, milestone_flags)
  return table.concat(out)
end

function novaria_on_sim_command(payload)
  local version, offset = read_u8(payload, 1)
  local command
  command, offset = read_u8(payload, offset)
  if version ~= RPC_VERSION or command == nil then
    error("invalid simrpc header")
  end

  if command == CMD_VALIDATE then
    return encode_validate_response(true)
  end

  if command == CMD_ACTION_PRIMARY then
    local player_id; player_id, offset = read_varuint(payload, offset)
    local player_tile_x; player_tile_x, offset = read_varint(payload, offset)
    local player_tile_y; player_tile_y, offset = read_varint(payload, offset)
    local target_tile_x; target_tile_x, offset = read_varint(payload, offset)
    local target_tile_y; target_tile_y, offset = read_varint(payload, offset)
    local hotbar_row; hotbar_row, offset = read_u8(payload, offset)
    local hotbar_slot; hotbar_slot, offset = read_u8(payload, offset)
    local inv_dirt; inv_dirt, offset = read_varuint(payload, offset)
    local inv_stone; inv_stone, offset = read_varuint(payload, offset)
    local inv_wood; inv_wood, offset = read_varuint(payload, offset)
    local inv_coal; inv_coal, offset = read_varuint(payload, offset)
    local inv_torch; inv_torch, offset = read_varuint(payload, offset)
    local inv_workbench; inv_workbench, offset = read_varuint(payload, offset)
    local inv_wood_sword; inv_wood_sword, offset = read_varuint(payload, offset)
    local has_pickaxe; has_pickaxe, offset = read_u8(payload, offset)
    local has_axe; has_axe, offset = read_u8(payload, offset)
    local target_is_air; target_is_air, offset = read_u8(payload, offset)
    local harvest_ticks; harvest_ticks, offset = read_varuint(payload, offset)
    local harvest_flags; harvest_flags, offset = read_u8(payload, offset)

    if player_id == nil or player_tile_x == nil or harvest_flags == nil then
      error("invalid action_primary request")
    end

    local harvest_by_pickaxe = (harvest_flags % 2) == 1
    local harvest_by_axe = (math.floor(harvest_flags / 2) % 2) == 1
    local harvest_by_sword = (math.floor(harvest_flags / 4) % 2) == 1

    if hotbar_row == 0 and hotbar_slot == 0 then
      if has_pickaxe ~= 0 and harvest_by_pickaxe and harvest_ticks > 0 then
        return encode_action_primary_response(ACTION_HARVEST, PLACE_NONE, harvest_ticks)
      end
      return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
    end

    if hotbar_row == 0 and hotbar_slot == 1 then
      if has_axe ~= 0 and harvest_by_axe and harvest_ticks > 0 then
        return encode_action_primary_response(ACTION_HARVEST, PLACE_NONE, harvest_ticks)
      end
      return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
    end

    if hotbar_row == 0 and hotbar_slot == 6 then
      if inv_wood_sword > 0 and harvest_by_sword and harvest_ticks > 0 then
        return encode_action_primary_response(ACTION_HARVEST, PLACE_NONE, harvest_ticks + 10)
      end
      return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
    end

    if hotbar_row == 0 and hotbar_slot == 2 then
      if target_is_air ~= 0 and inv_dirt > 0 then
        return encode_action_primary_response(ACTION_PLACE, PLACE_DIRT, 8)
      end
      return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
    end

    if hotbar_row == 0 and hotbar_slot == 3 then
      if target_is_air ~= 0 and inv_stone > 0 then
        return encode_action_primary_response(ACTION_PLACE, PLACE_STONE, 8)
      end
      return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
    end

    if hotbar_row == 0 and hotbar_slot == 4 then
      if target_is_air ~= 0 and inv_torch > 0 then
        return encode_action_primary_response(ACTION_PLACE, PLACE_TORCH, 8)
      end
      return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
    end

    if hotbar_row == 0 and hotbar_slot == 5 then
      if target_is_air ~= 0 and inv_workbench > 0 then
        return encode_action_primary_response(ACTION_PLACE, PLACE_WORKBENCH, 8)
      end
      return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
    end

    return encode_action_primary_response(ACTION_REJECT, PLACE_NONE, 0)
  end

  if command == CMD_CRAFT_RECIPE then
    local player_id; player_id, offset = read_varuint(payload, offset)
    local player_tile_x; player_tile_x, offset = read_varint(payload, offset)
    local player_tile_y; player_tile_y, offset = read_varint(payload, offset)
    local recipe_index; recipe_index, offset = read_u8(payload, offset)
    local workbench_reachable; workbench_reachable, offset = read_u8(payload, offset)
    local inv_dirt; inv_dirt, offset = read_varuint(payload, offset)
    local inv_stone; inv_stone, offset = read_varuint(payload, offset)
    local inv_wood; inv_wood, offset = read_varuint(payload, offset)
    local inv_coal; inv_coal, offset = read_varuint(payload, offset)
    local inv_torch; inv_torch, offset = read_varuint(payload, offset)
    local inv_workbench; inv_workbench, offset = read_varuint(payload, offset)
    local inv_wood_sword; inv_wood_sword, offset = read_varuint(payload, offset)

    if player_id == nil or recipe_index == nil then
      error("invalid craft_recipe request")
    end

    if recipe_index == 0 then
      if inv_wood < 3 then
        return encode_craft_recipe_response(CRAFT_REJECT)
      end
      return encode_craft_recipe_response(
        CRAFT_CRAFT,
        0, 0, -3, 0, 0, 1, 0,
        CRAFTED_WORKBENCH,
        true,
        false)
    end

    if recipe_index == 1 then
      if inv_wood < 7 or workbench_reachable == 0 then
        return encode_craft_recipe_response(CRAFT_REJECT)
      end
      return encode_craft_recipe_response(
        CRAFT_CRAFT,
        0, 0, -7, 0, 0, 0, 1,
        CRAFTED_NONE,
        false,
        true)
    end

    if recipe_index == 2 then
      if inv_wood < 1 or inv_coal < 1 then
        return encode_craft_recipe_response(CRAFT_REJECT)
      end
      return encode_craft_recipe_response(
        CRAFT_CRAFT,
        0, 0, -1, -1, 4, 0, 0,
        CRAFTED_TORCH,
        false,
        false)
    end

    return encode_craft_recipe_response(CRAFT_REJECT)
  end

  error("unknown simrpc command")
end
