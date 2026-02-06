# Novaria 运行时接口契约（Day 2）

## 目标

定义 `sim/world/net/script` 四层边界，保证后续开发并行可控，不破坏主循环。

## `sim`

- `sim::TickContext`：统一 Tick 输入（`tick_index`、`fixed_delta_seconds`）。
- `sim::SimulationKernel`：固定顺序编排：
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
- 当前实现：`world::WorldServiceBasic`
  - 内存 Chunk 容器
  - 基础地表生成
  - 支持负坐标 Tile 写入

## `net`

- `net::INetService` 契约：
  - `Initialize/Shutdown`
  - `Tick`
  - `SubmitLocalCommand`
  - `PublishWorldSnapshot`
- 当前实现：`net::NetServiceStub`

## `script`

- `script::IScriptHost` 契约：
  - `Initialize/Shutdown`
  - `Tick`
  - `DispatchEvent`
- 当前实现：`script::ScriptHostStub`

## 当前状态

- `GameApp` 已接入 `SimulationKernel` 固定更新入口。
- `world` 已从 Stub 升级为最小可运行实现。
- 已接入 `CTest` 与 `world_service_basic` 单元测试入口。
