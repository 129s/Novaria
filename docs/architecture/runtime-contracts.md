# Novaria 运行时契约

- `status`: authoritative
- `owner`: @novaria-core
- `last_verified_commit`: 69a8ef7
- `updated`: 2026-02-06

## 1. 目标

定义 `sim/world/net/script/save/mod` 的边界，确保实现可并行推进且可测试。

## 2. 调度主线（`sim::SimulationKernel`）

固定调度顺序：

1. 转发本地命令到 `net`。
2. 执行可识别世界命令（`world.set_tile`、`world.load_chunk`、`world.unload_chunk`）。
3. 执行可识别玩法命令（采集/建造/制作/战斗/Boss）。
4. `net.Tick`。
5. （仅连接态）消费远端快照并应用到 `world`。
6. `world.Tick`。
7. `script.Tick`。
8. `world` 输出脏块并编码。
9. （仅连接态）`net.PublishWorldSnapshot`。

## 3. 分模块契约

### 3.1 `sim`

**契约**

- `TickContext` 提供统一 Tick 输入（`tick_index`、`fixed_delta_seconds`）。
- `SimulationKernel` 提供：
  - `ApplyRemoteChunkPayload`（远端快照应用入口）
  - 本地命令队列上限保护
  - 玩法进度快照读写（`GameplayProgress` / `RestoreGameplayProgress`）

**当前实现**

- 自动重连节流：`kAutoReconnectRetryIntervalTicks`。
- 会话事件限流与合并：`kSessionStateEventMinIntervalTicks`。
- 会话变化事件：`net.session_state_changed`，payload `state=...;tick=...;reason=...`。
- 玩法里程碑事件：`gameplay.progress`，payload `milestone=...;tick=...`。

### 3.2 `world`

**契约**

- `IWorldService`：
  - `Initialize/Shutdown`
  - `Tick`
  - `LoadChunk/UnloadChunk`
  - `ApplyTileMutation`
  - `BuildChunkSnapshot`
  - `ApplyChunkSnapshot`
  - `ConsumeDirtyChunks`

**当前实现**

- `WorldServiceBasic`（内存 Chunk 容器 + 基础地形生成）。
- 脏块增量追踪，`ConsumeDirtyChunks` 稳定排序输出。
- 支持负坐标 Tile 读写。

### 3.3 `net`

**契约**

- `INetService`：
  - `Initialize/Shutdown`
  - `RequestConnect/RequestDisconnect`
  - `NotifyHeartbeatReceived`
  - `SessionState` / `DiagnosticsSnapshot`
  - `Tick`
  - `SubmitLocalCommand`
  - `ConsumeRemoteChunkPayloads`
  - `PublishWorldSnapshot`

**当前实现**

- `NetServiceRuntime`：固定走 `udp_loopback` 后端（无 stub/无自动回退）。
- `udp_loopback` 后端：`NetServiceUdpLoopback`（支持本地端口绑定与对端端点配置，可做同机双进程互通验证）。
- `udp_loopback` 已引入最小握手与心跳控制报文（`SYN/ACK/HEARTBEAT`）。
- 心跳超时断线：`kHeartbeatTimeoutTicks`。
- 断线态拒收本地命令与远端 payload。
- 提供可观测诊断：迁移计数、丢弃计数、最近迁移原因、最后心跳 Tick。
- `UdpTransport` 已提供可绑定端口、非阻塞收发、Loopback 自测能力（基础传输层骨架）。

### 3.4 `script`

**契约**

- `IScriptHost`：
  - `Initialize/Shutdown`
  - `Tick`
  - `DispatchEvent`

**当前实现**

- `ScriptHostRuntime`：固定走 `luajit` 后端（无 stub/无自动回退）。
- `LuaJitScriptHost` 已完成 VM 生命周期与事件回调骨架（`novaria_on_tick` / `novaria_on_event`）。
- `LuaJitScriptHost` 已启用 MVP 最小沙箱（禁用 `io/os/debug/package/dofile/loadfile/load/require/collectgarbage`）。
- `IScriptHost::RuntimeDescriptor` 已统一暴露 `backend/api_version/sandbox` 元信息（当前 API 版本 `0.1.0`）。
- 已支持按模组清单字段 `script_entry/script_api_version` 装载内容脚本并进行 API 版本 fail-fast 校验。
- 当前仍未完成生产级脚本沙箱策略（资源配额、隔离等级等）。

### 3.5 `save`

**契约**

- `ISaveRepository`：
  - `Initialize/Shutdown`
  - `SaveWorldState/LoadWorldState`

**当前实现**

- `FileSaveRepository`：
  - `format_version` 版本字段，支持旧档兼容与前向版本拒绝。
  - 按需写入 `gameplay_section.core.*`（含 `gameplay_section.core.version`）。
  - 写入 `debug_section.net.*`（含 `debug_section.net.version`），并兼容旧 `debug_net_*` 字段。
  - 存档指纹校验受 `strict_save_mod_fingerprint` 控制。

### 3.6 `mod`

**契约**

- `ModLoader`：
  - 扫描 `mods/*/mod.toml`
  - 解析 `name/version/description/dependencies`
  - 依赖拓扑排序装载
  - 缺失依赖与循环依赖直接失败
  - 计算稳定指纹

**当前实现**

- 可选解析内容定义：
  - `content/items.csv`
  - `content/recipes.csv`
  - `content/npcs.csv`
- 可选解析脚本入口元信息：`script_entry`、`script_api_version`（用于脚本运行时装载与一致性校验）。
- 指纹已纳入依赖、内容定义与脚本元信息，支持联机一致性校验。

## 4. 输入映射（调试）

- `J`：提交 `jump`
- `K`：提交 `attack`
- `F1`：发送 `debug.ping`
- `F2`：提交 `world.set_tile`（`0,0,0`）
- `F3`：提交 `world.set_tile`（`0,0,2`）
- `F4`：请求断线
- `F5`：注入心跳
- `F6`：请求重连
- `F7`：采集木材（+5）
- `F8`：采集石材（+5）
- `F9`：建造工作台
- `F10`：制作剑
- `F11`：攻击基础敌人
- `F12`：攻击 Boss
