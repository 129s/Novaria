# 模块边界与契约

## `sim`

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

## `world`

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

## `net`

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
- `udp_loopback` 后端支持本地绑定地址配置（`127.0.0.1`/`0.0.0.0`），可推进到跨主机联调。
- `udp_loopback` 已引入最小握手与心跳控制报文（`SYN/ACK/HEARTBEAT`）。
- 已引入连接探测指数退避（`kConnectProbeIntervalTicks` -> `kMaxConnectProbeIntervalTicks`）并提供探测诊断计数。
- 已引入对端来源校验与动态对端采纳（配置 `remote_port=0` 时可由 `SYN` 建立 peer）。
- UDP 不可达噪声（Windows `WSAECONNRESET`）按可恢复空读处理，避免误判为传输层硬错误。
- 心跳超时断线：`kHeartbeatTimeoutTicks`。
- 断线态拒收本地命令与远端 payload。
- 提供可观测诊断：迁移计数、探测计数、来源过滤计数、丢弃计数、最近迁移原因、最后心跳 Tick。
- `UdpTransport` 已提供可绑定端口、非阻塞收发、Loopback 自测能力（基础传输层骨架）。

## `script`

**契约**

- `IScriptHost`：
  - `Initialize/Shutdown`
  - `Tick`
  - `DispatchEvent`

**当前实现**

- `ScriptHostRuntime`：固定走 `luajit` 后端（无 stub/无自动回退）。
- `LuaJitScriptHost` 已完成 VM 生命周期与事件回调骨架（`novaria_on_tick` / `novaria_on_event`）。
- `LuaJitScriptHost` 已启用 MVP 最小沙箱（禁用 `io/os/debug/package/dofile/loadfile/load/require/collectgarbage`）。
- 脚本执行已加每次调用指令预算保护，超预算直接 fail-fast（避免死循环拖垮主线程）。
- 脚本 VM 已启用内存配额分配器（固定预算上限），超配额会在 Lua 调用边界 fail-fast。
- 模组脚本按模块独立环境装载，仅导出受控回调（`novaria_on_tick/novaria_on_event`）。
- `IScriptHost::RuntimeDescriptor` 已统一暴露 `backend/api_version/sandbox` 元信息（当前 API 版本 `0.1.0`）。
- 已支持按模组清单字段 `script_entry/script_api_version` 装载内容脚本并进行 API 版本 fail-fast 校验。
- 当前仍未完成最终形态沙箱策略（细粒度 capability 白名单、跨模组权限配置、配额可配置化）。

## `save`

**契约**

- `ISaveRepository`：
  - `Initialize/Shutdown`
  - `SaveWorldState/LoadWorldState`

**当前实现**

- `FileSaveRepository`：
  - `format_version` 版本字段，支持旧档兼容与前向版本拒绝。
  - 按需写入 `gameplay_section.core.*`（含 `gameplay_section.core.version`）。
  - 按需写入 `world_section.core.*`（含 `world_section.core.version/chunk_count/chunk.<index>`）。
  - 写入 `debug_section.net.*`（含 `debug_section.net.version`），并兼容旧 `debug_net_*` 字段。
  - 存档写入采用 `world.sav.tmp -> world.sav` 原子替换，并保留 `world.sav.bak` 回档副本。
  - 存档指纹校验受 `strict_save_mod_fingerprint` 控制。

## `mod`

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
- 可选解析脚本能力声明：`script_capabilities`（用于运行时能力校验）。
- 指纹已纳入依赖、内容定义与脚本元信息（含能力声明），支持联机一致性校验。

## `ecs`

**契约**

- `ecs::Runtime` 仅承载行为实体（当前：`projectile/hostile_target`）。
- `world(tile/chunk)` 继续作为权威世界数据层，不迁入 `entt::registry`。
- ECS 系统顺序固定：`spawn -> movement -> collision -> damage -> lifetime_recycle`。
- ECS 对外仅暴露强类型输入与事件输出：
  - 输入：`combat.fire_projectile`（经 `TypedPlayerCommand` 解码）。
  - 输出：`CombatEvent`（当前实现 `HostileDefeated`）。

**当前实现**

- 已接入 EnTT 运行时骨架：`sim::ecs::Runtime`。
- 已落地投射物纵切：生成→移动→碰撞→伤害→回收。
- 已与 `SimulationKernel` 固定 Tick 调度对齐，并将击杀事件回灌玩法进度计数。
