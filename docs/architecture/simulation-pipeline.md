# Simulation Pipeline

## 目标

定义 `sim/world/net/script/ecs` 的**固定 Tick**调度顺序与跨模块时序约束，保证行为可预测、可测试、可复盘。

## 名词

- **Tick**：固定步长推进单位，由 `sim::TickContext{tick_index, fixed_delta_seconds}` 表达。
- **Authority**：权威仿真模式，接收命令并推进世界，负责发布世界快照。
- **Replica**：副本仿真模式，不执行远端命令，只消费远端世界快照并应用到 world。

## 固定调度顺序（`sim::SimulationKernel::Update`）

> 下述顺序是契约：任何变更必须同步更新该文档与对应自动化验证。

### 1) 连接维护（可选）

- 在断线态按节流策略触发 `net.RequestConnect`（由仿真 Tick 驱动，而非平台线程）。

### 2) 注入本地命令 → `net`

- 将本地命令队列转发给 `net.SubmitLocalCommand`（本地输入统一走 net 层以保持一致路径）。

### 3) `net.Tick`

- 处理握手/心跳/重连/收发队列。

### 4) 消费网络输入

- **Authority**：
  - `net.ConsumeRemoteCommands` → 解码为 `TypedPlayerCommand`
  - 依次执行可识别命令：
    - world 命令：`world.set_tile / world.load_chunk / world.unload_chunk`
    - gameplay/ecs 命令：采集/掉落/拾取/战斗等（应逐步拆为 system/ruleset）
- **Replica**：
  - 在连接态：`net.ConsumeRemoteChunkPayloads` → `ApplyRemoteChunkPayload` → `world.ApplyChunkSnapshot`

### 5) 会话状态事件（可观测）

- 从 `net.DiagnosticsSnapshot` 读取 `session_state/last_transition_reason`，在状态变化时生成并限流分发会话事件（例如 `net.session_state_changed`）。

### 6) `world.Tick`

- 推进 world 内部状态（生成/加载策略/脏标记等）。

### 7) `ecs.Tick`

- 推进实体行为（投射物/碰撞/伤害/掉落/拾取等）。

### 8) 事件分发 → `script`（输入）

- 将可观测的玩法事件（例如拾取/交互）整理为事件并 `script.DispatchEvent`。

### 9) `script.Tick`

- 运行脚本 tick 回调，并消费事件队列。

### 10) 世界输出（脏块 → 编码）

- `world.ConsumeDirtyChunks` → `world.BuildChunkSnapshot` → `WorldSnapshotCodec::EncodeChunkSnapshot`

### 11) 发布快照 → `net`（仅 Authority 且连接态）

- `net.PublishWorldSnapshot(tick_index, encoded_dirty_chunks)`

### 12) Tick 收尾

- `tick_index++`

## 调度约束（强约束）

- **顺序不可随意交换**：
  - `net.Tick` 必须早于消费网络输入（保证队列状态一致）。
  - `world.Tick` 必须早于世界脏块输出（否则输出与推进不同步）。
  - `script.Tick` 必须晚于核心仿真（脚本只观察/响应，不得抢先改变权威结果）。
- **连接态门禁**：
  - Replica 只在连接态消费远端快照。
  - Authority 只在连接态发布世界快照。
- **可复盘性**：
  - Tick 内的跨模块副作用必须通过可追溯事件/诊断暴露（至少：会话变更、关键玩法里程碑、丢弃/限流计数）。

