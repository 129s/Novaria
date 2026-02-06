# Novaria 运行时接口契约（Day 2）

## 目标

定义 `sim/world/net/script/save/mod` 的边界，确保实现可并行推进且可测试。

## `sim`

- `sim::TickContext`：统一 Tick 输入（`tick_index`、`fixed_delta_seconds`）。
- `sim::SimulationKernel`：固定调度顺序：
  0. 转发本地命令到 `net`
  1. 执行可识别的世界命令（`world.set_tile`、`world.load_chunk`、`world.unload_chunk`）
  2. `net.Tick`
  3. （仅已连接）消费 `net` 远端快照 payload 并应用到 `world`
  4. `world.Tick`
  5. `script.Tick`
  6. `world` 生成脏块快照并编码
  7. （仅已连接）`net.PublishWorldSnapshot`
- `sim::SimulationKernel` 额外提供远端快照应用入口：`ApplyRemoteChunkPayload`。
- `sim::SimulationKernel` 本地命令队列提供上限保护：超出 `kMaxPendingLocalCommands` 的输入会被丢弃并计数。
- `sim::SimulationKernel` 初始化会请求 `net` 建连，运行时检测到断线会自动请求重连。
- 自动重连请求带 Tick 级节流（`kAutoReconnectRetryIntervalTicks`）以避免重连风暴。
- `sim::SimulationKernel` 检测到 `net` 会话状态变化时，会向脚本层派发 `net.session_state_changed` 事件（payload: `state,tick,reason`）。

## `world`

- `world::IWorldService` 契约：
  - `Initialize/Shutdown`
  - `Tick`
  - `LoadChunk/UnloadChunk`
  - `ApplyTileMutation`
  - `BuildChunkSnapshot`
  - `ApplyChunkSnapshot`
  - `ConsumeDirtyChunks`
- 当前实现：`world::WorldServiceBasic`
  - 内存 Chunk 容器
  - 基础地表生成
  - 负坐标 Tile 写入
  - 增量脏块集合追踪（`ConsumeDirtyChunks` 不扫描全量 Chunk）
  - `ConsumeDirtyChunks` 按坐标稳定排序，便于复制链路复现

## `net`

- `net::INetService` 契约：
  - `Initialize/Shutdown`
  - `RequestConnect/RequestDisconnect`
  - `NotifyHeartbeatReceived`
  - `SessionState`
  - `DiagnosticsSnapshot`
  - `Tick`
  - `SubmitLocalCommand`
  - `PublishWorldSnapshot(tick_index, encoded_dirty_chunks)`
- 当前实现：`net::NetServiceStub`
  - 会话状态机：`Disconnected -> Connecting -> Connected`
  - 会话可观测：状态迁移会输出日志，并提供迁移计数/连接计数/断线原因计数与最近迁移原因
  - 心跳超时：超过 `kHeartbeatTimeoutTicks` 未更新心跳将断线
  - 断线恢复入口：`RequestConnect` 重新发起连接
  - 断线拒收：`Disconnected` 状态下拒绝本地命令与远端 payload 入队
  - 本地命令入队
  - 队列上限与丢弃计数（防输入风暴）
  - 远端快照 payload 入队与消费（含队列上限与丢弃计数）
  - Tick 内批处理命令
  - 快照发布统计
- `app::GameApp` 会按固定周期输出 `net` 健康快照（状态、迁移计数、丢弃计数、心跳忽略计数）。

## `script`

- `script::IScriptHost` 契约：
  - `Initialize/Shutdown`
  - `Tick`
  - `DispatchEvent`
- 当前实现：`script::ScriptHostStub`
  - 事件入队
  - 队列上限与丢弃计数（防事件风暴）
  - Tick 内批处理事件
  - 事件处理统计

## `save`

- `save::ISaveRepository` 契约：
  - `Initialize/Shutdown`
  - `SaveWorldState/LoadWorldState`
- 当前实现：`save::FileSaveRepository`
  - 启动读取 `saves/world.sav`
  - 存档字段包含 `format_version`，支持旧档（缺失版本字段）兼容与前向版本拒绝
  - 退出写回当前 Tick、本地玩家编号与模组清单指纹
  - 调试快照头：持久化 `debug_net_*` 计数用于联机问题复盘
  - 配置 `strict_save_mod_fingerprint=true` 时，读档指纹不一致将拒绝启动

## `mod`

- `mod::ModLoader`：
  - 扫描 `mods/*/mod.toml`
  - 解析 `name/version/description`
  - 生成稳定清单指纹（order-insensitive）用于后续联机一致性校验

## 当前输入映射（调试）

- `J`：提交 `jump` 命令（保留）
- `K`：提交 `attack` 命令（保留）
- `F1`：发送 `debug.ping` 脚本事件
- `F2`：提交 `world.set_tile`（`0,0,0`，挖空）
- `F3`：提交 `world.set_tile`（`0,0,2`，放置石块）
- `F4`：请求 `net` 断开连接（调试）
- `F5`：注入一次心跳（调试）
- `F6`：请求 `net` 重新连接（调试）
