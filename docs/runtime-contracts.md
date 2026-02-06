# Novaria 运行时接口契约（Day 2）

## 目标

定义 `sim/world/net/script` 四层边界，保证后续开发并行可控，不破坏主循环。

## `sim`

- `sim::TickContext`：统一 Tick 输入（`tick_index`、`fixed_delta_seconds`）。
- `sim::SimulationKernel`：固定顺序编排：
  0. 转发本地命令到 `net`
  1. `net.Tick`
  2. `world.Tick`
  3. `script.Tick`
  4. `net.PublishWorldSnapshot`

## `world`

- `world::IWorldService` 契约：
  - `Initialize/Shutdown`
  - `Tick`
  - `LoadChunk/UnloadChunk`
  - `ApplyTileMutation`
  - `ConsumeDirtyChunks`
- 当前实现：`world::WorldServiceBasic`
  - 内存 Chunk 容器
  - 基础地表生成
  - 支持负坐标 Tile 写入

## `net`

- `net::INetService` 契约：
  - `Initialize/Shutdown`
  - `Tick`
  - `SubmitLocalCommand`
  - `PublishWorldSnapshot(tick_index, dirty_chunk_count)`
- 当前实现：`net::NetServiceStub`
  - 本地命令入队
  - Tick 内批处理命令
  - 快照发布计数与最后 Tick 记录

## `script`

- `script::IScriptHost` 契约：
  - `Initialize/Shutdown`
  - `Tick`
  - `DispatchEvent`
- 当前实现：`script::ScriptHostStub`
  - 事件入队
  - Tick 内批处理事件
  - 事件处理计数

## 当前状态

- `GameApp` 已接入 `SimulationKernel` 固定更新入口。
- 本地输入已接入命令通道：`J` 触发 `jump`，`K` 触发 `attack`，`F1` 触发 `debug.ping` 事件。
- `world` 已从 Stub 升级为最小可运行实现。
- 已接入 `CTest` 与 `world_service_basic` 单元测试入口。
- 已补 `SimulationKernel` 编排与回滚测试。
