# Wire Protocol v1（二进制）

## 目标

为 `net` 传输与 `save` 持久化提供**单一、可演进、可复盘**的数据表示，彻底消灭跨模块“手搓字符串协议”：

- 编码必须**单射**（同一语义只有一种字节表示，不允许分隔符拼接造成歧义/伪造）。
- 必须有**版本字段**与明确的兼容策略。
- 编解码必须**集中化**：任何模块不得自行拼接/解析 payload。

## 基础编码（v1 规范）

### VarUInt（无符号变长整数）

- 使用 **ULEB128** 编码。
- 最高位为延续位：`byte & 0x80 != 0` 表示后续还有字节。

### VarInt（有符号变长整数）

- 先做 **ZigZag** 映射到无符号，再用 VarUInt 编码：
  - `zigzag(x) = (x << 1) ^ (x >> 63)`（以 64-bit 语义描述，实际按目标位宽实现）。

### Bytes / String

- `bytes`：`VarUInt length` + `length` 个原始字节。
- `string`：UTF-8 `bytes`（同上）。协议层不允许以 `'\0'` 结尾约定。

## Datagram Envelope（网络帧）

> 每个 UDP datagram 必须携带一个 envelope。v1 不要求额外 magic；靠 `wire_version` 过滤即可。

字段顺序（严格）：

1. `u8 wire_version`：固定为 `1`
2. `u8 kind`：消息类型
3. `VarUInt payload_len`
4. `payload_bytes`（长度为 `payload_len`）

### kind 枚举（v1）

| kind | 名称 | 说明 |
| --- | --- | --- |
| 1 | `control` | 握手/心跳等控制面 |
| 2 | `command` | 玩家命令（权威输入） |
| 3 | `chunk_snapshot` | 单个 chunk 快照 |
| 4 | `chunk_snapshot_batch` | 多个 chunk 快照打包（建议优先使用） |

> 规则：未知 `kind` 必须丢弃；不得尝试“尽力解析”。

## Payload 结构（v1）

### 1) control

- `u8 control_type`

| control_type | 名称 |
| --- | --- |
| 1 | `SYN` |
| 2 | `ACK` |
| 3 | `HEARTBEAT` |

> 备注：v1 control 不携带 tick。若需要诊断可在后续版本扩展字段（保持向后兼容）。

### 2) command

- `VarUInt player_id`
- `VarUInt command_id`
- `bytes command_payload`（按 `command_id` 解释）

#### command_id（v1 固定表）

> 规则：command_id 一旦发布不可更改；只能追加新 id。

| command_id | 语义 |
| --- | --- |
| 1 | `jump` |
| 2 | `attack` |
| 3 | `player.motion_input` |
| 10 | `world.set_tile` |
| 11 | `world.load_chunk` |
| 12 | `world.unload_chunk` |
| 20 | `gameplay.collect_resource` |
| 21 | `gameplay.spawn_drop` |
| 22 | `gameplay.pickup_probe` |
| 23 | `gameplay.interaction` |
| 24 | `gameplay.action_primary` |
| 25 | `gameplay.craft_recipe` |
| 26 | `gameplay.attack_enemy` |
| 27 | `gameplay.attack_boss` |
| 30 | `combat.fire_projectile` |

#### command_payload（v1）

- `world.set_tile`：
  - `VarInt tile_x`
  - `VarInt tile_y`
  - `VarUInt material_id`（与 material catalog 对齐）
- `world.load_chunk` / `world.unload_chunk`：
  - `VarInt chunk_x`
  - `VarInt chunk_y`
- `gameplay.collect_resource`：
  - `VarUInt resource_id`
  - `VarUInt amount`
- `gameplay.spawn_drop`：
  - `VarInt tile_x`
  - `VarInt tile_y`
  - `VarUInt material_id`
  - `VarUInt amount`
- `gameplay.pickup_probe`：
  - `VarInt tile_x`
  - `VarInt tile_y`
- `gameplay.interaction`：
  - `VarUInt interaction_type`
  - `VarInt target_tile_x`
  - `VarInt target_tile_y`
  - `VarUInt target_material_id`
  - `VarUInt result_code`
- `player.motion_input`：
  - `VarInt move_axis_milli`（范围 `[-1000, 1000]`；`move_axis = move_axis_milli / 1000`）
  - `VarUInt input_flags`（bit0 = `jump_pressed`）
- `gameplay.action_primary`：
  - `VarInt target_tile_x`
  - `VarInt target_tile_y`
  - `VarUInt hotbar_row`
  - `VarUInt hotbar_slot`
- `gameplay.craft_recipe`：
  - `VarUInt recipe_index`
- `gameplay.attack_enemy` / `gameplay.attack_boss`：
  - payload 为空（`payload_len = 0`）
- `combat.fire_projectile`：
  - `VarInt origin_tile_x`
  - `VarInt origin_tile_y`
  - `VarInt velocity_milli_x`
  - `VarInt velocity_milli_y`
  - `VarUInt damage`
  - `VarUInt lifetime_ticks`
  - `VarUInt faction`

> 规则：未知 `command_id` 必须丢弃该 command；不得尝试兼容旧字符串类型。

### 3) chunk_snapshot

- `VarInt chunk_x`
- `VarInt chunk_y`
- `VarUInt tile_count`（必须等于 `world::kChunkTileSize * world::kChunkTileSize`）
- `bytes tiles_u16_le`（长度必须等于 `tile_count * 2`，每个 tile 为 little-endian `u16 material_id`）

### 4) chunk_snapshot_batch

- `VarUInt chunk_count`
- 依次拼接 `chunk_snapshot`（不含 envelope），重复 `chunk_count` 次

> 建议：batch 的 chunk_total_bytes 不应超过单个 UDP datagram 的可达上限（按 MTU 估算），超出必须拆包。

## Save（持久化）要求

- 存档中涉及快照的部分必须复用 v1 的 `chunk_snapshot_batch` 或 `chunk_snapshot` payload（使用 base64/hex 存储均可）。
- 禁止在 save 中使用 CSV/逗号分隔来编码 tiles（该类编码不单射且难以版本化）。

## 兼容策略（本项目约束）

由于当前环境为非生产，允许一次性迁移：

- v1 上线后：`net` 与 `save` **只接受 v1**。
- legacy（字符串协议）不需要双栈兼容；发现 legacy 输入直接失败并打印清晰错误。

> 注意：即便允许“直接迁移”，也必须保留版本字段与未知 kind/command 的丢弃策略，否则下一次演进还是会炸。
