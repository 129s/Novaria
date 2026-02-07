# Novaria Progress

最后更新：2026-02-07

## Status

- 当前里程碑：**M1 重构**（先把边界、诊断、测试与工程化打稳，再推进体验与内容）。
- 主要风险（会阻断联调/演进）：
  - 跨主机 UDP 可能出现“误判为 self，从而不发包”的行为（见 I-001）。
  - `PlayerController` 与 `SimulationKernel` 同时承担过多职责，且存在玩法规则复制，任何改动都容易引发连锁回归（见 I-004/I-005/I-014）。
  - 测试体系存在并行冲突与 perf 抖动门禁，容易出现“代码没坏但测试随机红”的低效状态（见 I-012/I-013）。
- 文档分工（强约束）：
  - `docs/architecture/*` 只写**契约与约束**（可作为长期稳定的设计事实源）。
  - `docs/status/*` 只写**当前状态、里程碑、问题单与验收口径**（短期变化频繁的事实源）。

## Milestones

> 规则：里程碑只表达“阶段目标 + 退出标准”，不描述实现细节；实现细节必须落在 Issue 的验收标准里。

| 编号 | 阶段 | 状态 | 阶段目标 | 退出标准（DoD） |
| --- | --- | --- | --- | --- |
| M1 | 重构（边界与可测性） | 进行中 | 把依赖方向、模块边界、诊断语义与测试稳定性收口，避免“越做越难改”。 | 1) `app/platform` 不依赖具体 world 实现；2) Net 诊断指标语义一致；3) `ctest -j` 稳定；4) perf 用例不作为默认门禁；5) `PlayerController` 由可替换子组件构成且有回归覆盖。 |
| M2 | 可玩闭环 | 待开始 | 在 M1 基础上，形成可重复验证的最小可玩路径，并给出清晰的人工体验验收清单。 | 1) 自动化：验收/端到端用例通过；2) 人工：闭环流程可达、关键交互手感达标；3) 存档回读不破坏闭环；4) 发布包可复现构建并可启动。 |

## Issues

> 规则：
> 1) Issue 必须可验收：每条都要写“验收标准”；没有验收标准的 Issue 视为未定义。
> 2) `P0`：阻断联调/会导致错误行为/会导致大规模返工；`P1`：近期不修会显著拖慢；`P2`：长期收益但不阻断。
> 3) 状态枚举：`TODO / DOING / DONE / BLOCKED`（默认 `TODO`）。

### M1（重构）

#### P0

- **I-001**：UDP “self” 误判导致跨主机不发包
  - 范围：`net`
  - 状态：TODO
  - 现象：当本地绑定 `0.0.0.0` 时，远端端点可能被误判为 self，导致 `SubmitLocalCommand()` 直接跳过发送。
  - 证据：`src/net/net_service_udp_loopback.cpp:526`（`IsSelfEndpoint`）、`src/net/net_service_udp_loopback.cpp:431`（self 直接 return）。
  - 验收标准：
    - 配置 `bind_host=0.0.0.0` 的跨主机场景下，命令与快照 datagram 能稳定发出并被对端接收。
    - 新增覆盖该场景的自动化验证（单测或工具级 smoke）。

#### P1

- **I-002**：Net 诊断指标语义不一致（“看似 dropped，实际已执行/已入队”）
  - 范围：`net`、`sim`
  - 状态：TODO
  - 现象：断线态/未连接态下本地命令可能仍进入处理队列，但同时计入 dropped 指标，导致诊断误导与门禁误判。
  - 证据：`src/net/net_service_udp_loopback.cpp:412`（`SubmitLocalCommand` 先入队再计 dropped）。
  - 验收标准：
    - `dropped_*` 只统计“实际丢弃（未进入任何处理路径）”。
    - `unsent_* / suppressed_send_*`（或等价命名）用于描述“未发送到远端”的情况。
    - `docs/engineering/release-and-diagnostics.md`（如有）或日志输出口径同步更新。

- **I-003**：`world` 实现细节泄漏到 `app/platform`，模块边界失效
  - 范围：`world`、`app`、`platform`
  - 状态：TODO
  - 现象：上层直接依赖 `WorldServiceBasic::kMaterial*`、`TryReadTile` 等实现细节，导致 world 替换与演进成本极高。
  - 证据：`src/platform/sdl_context.cpp:4`、`src/app/player_controller.h:5`、`src/app/render_scene_builder.h:6`。
  - 验收标准：
    - `platform` 不包含任何 world 具体实现头文件。
    - `app` 只依赖 `world::IWorldService`（以及稳定的材料/渲染元信息接口），不依赖 `WorldServiceBasic` 常量。
    - 材料定义从 world 实现中抽离为稳定概念（例如 `MaterialId` + traits/catalog）。

- **I-004**：`PlayerController` 上帝类拆分与表驱动
  - 范围：`app`
  - 状态：TODO
  - 现象：输入、状态机、chunk 窗口、采集/放置/交互/制作、HUD 状态等混杂在一个函数里，分支爆炸且难以测试。
  - 证据：`src/app/player_controller.cpp`（`PlayerController::Update`）。
  - 验收标准：
    - `PlayerController` 变为“编排器”，核心行为分解为可替换/可单测的子组件（至少：目标解析、动作执行、背包/热栏、智能选择、chunk 窗口）。
    - 热栏/工具/动作分支由数据结构或函数表驱动，显著减少 if/else 嵌套。
    - 现有端到端用例不回退，且新增单元级覆盖（不依赖 SDL）。

- **I-005**：玩法参数与门槛存在重复定义，可能产生“UI 认为可做但内核拒绝”的撕裂
  - 范围：`app`、`sim`
  - 状态：TODO
  - 证据：`src/app/player_controller.cpp:27`（成本常量）、`src/sim/simulation_kernel.cpp:16`（成本常量）。
  - 验收标准：
    - 玩法门槛/成本只存在一个权威来源（建议在 `sim` 或数据驱动层），UI 仅展示权威状态与提示。

- **I-006**：`SimulationKernel` 混合“调度 + 玩法规则 + 文本事件拼装”，演进风险高
  - 范围：`sim`
  - 状态：TODO
  - 证据：`src/sim/simulation_kernel.cpp`（玩法进度、Boss 规则、事件 payload 构建）。
  - 验收标准：
    - `SimulationKernel` 保持“调度与跨模块协调”的职责边界。
    - 玩法规则独立为可测试的 system / ruleset（可先做最小拆分，不要求一步到位数据驱动）。

- **I-014**：`GameApp` 装配层过度集中（启动/存档/模组/脚本/主循环绑死），难以复用与测试
  - 范围：`app`
  - 状态：TODO
  - 证据：`src/app/game_app.h:34`（聚合所有服务）、`src/app/game_app.cpp`（Initialize 同时做 config/save/mod/script 逻辑）。
  - 验收标准：
    - 把“装配/加载”拆为独立组件（例如 config/bootstrap/save/mod/script 各自模块化）。
    - `GameApp` 只保留编排与生命周期，不再承载具体加载细节。

- **I-012**：测试临时目录使用固定名称，`ctest -j` 并行会互相删除导致随机失败
  - 范围：`tests`
  - 状态：TODO
  - 证据：`tests/core/config_tests.cpp:18`、`tests/mod/mod_loader_tests.cpp:20`、`tests/save/save_repository_tests.cpp:18`、`tests/sim/simulation_kernel_tests.cpp:137`。
  - 验收标准：
    - 所有落盘测试目录均为唯一（至少包含 pid 或随机后缀）。
    - `ctest -j`（并行）在本机稳定通过。

- **I-013**：perf 用例作为默认门禁（Debug/不同机器抖动），降低研发效率
  - 范围：`tests`、`engineering`
  - 状态：TODO
  - 证据：`tests/mvp/mvp_acceptance_tests.cpp:316`（硬阈值 p95 ≤ 16.6ms）。
  - 验收标准：
    - perf 用例从默认 `ctest` 运行集中隔离（label/单独 target/显式开关）。
    - perf 只在约定构建配置（例如 `RelWithDebInfo`）下作为门禁。

#### P2

- **I-007**：配置文件名为 `.toml` 但解析器不是 TOML 子集，易产生“写了但没生效”
  - 范围：`core/config`
  - 状态：TODO
  - 证据：`src/core/config.cpp`（自定义 `key=value` 解析，未知 key 沉默）。
  - 验收标准（两条路径二选一）：
    - A) 引入真正的 TOML 解析并定义兼容子集；或
    - B) 明确更名并增加 unknown key 告警/严格模式。

- **I-008**：Mod 指纹用于一致性校验但采用 64-bit FNV，碰撞风险不可控
  - 范围：`mod`
  - 状态：TODO
  - 证据：`src/mod/mod_loader.cpp:213`（FNV-1a 64-bit）。
  - 验收标准：
    - 指纹升级为加密哈希（建议 SHA-256），并把其视为协议字段管理版本迁移。

- **I-009**：Mod 脚本装载辅助逻辑在 `game_app` 与 `server_main` 复制，易产生行为漂移
  - 范围：`app`、`tools`
  - 状态：TODO
  - 证据：`src/app/game_app.cpp` 与 `tools/server_main.cpp` 都包含 `ReadTextFile/IsSafeRelativePath/BuildModScriptModules`。
  - 验收标准：
    - 提取为单一实现（例如 `src/runtime/mod_script_loader.*`），两端共享。

- **I-010**：Lua bootstrap 脚本内嵌在 `.cpp` 中，不利于迭代与审计
  - 范围：`script`
  - 状态：TODO
  - 证据：`src/script/lua_jit_script_host.cpp`（`kBootstrapScript`）。
  - 验收标准：
    - bootstrap 脚本外置为文件，并在构建期嵌入二进制（发布不依赖外部文件）。
    - 允许开发态替换（可选，但推荐）以加速脚本迭代。

- **I-011**：架构文档与实际调度/依赖存在偏差，需要以契约形式收口
  - 范围：`docs/architecture`
  - 状态：DONE
  - 验收标准：
    - `docs/architecture/simulation-pipeline.md` 与实际 `SimulationKernel::Update` 语义一致。
    - `docs/architecture/module-contracts.md` 只保留契约，不包含“当前实现进度”。

- **I-015**：CMake 源文件在多个可执行重复编译，工程规模增长会被编译时间拖累
  - 范围：`build`
  - 状态：TODO
  - 证据：`CMakeLists.txt:175`（`novaria`）与 `CMakeLists.txt:228`（`novaria_server`）等重复列源。
  - 验收标准：
    - 模块拆分为库（static/object），可执行文件只链接，避免重复编译。

- **I-016**：缺少最小 CI（构建 + 测试）事实源，发布前质量靠人工兜底
  - 范围：`engineering`
  - 状态：TODO
  - 验收标准：
    - 至少提供一条可复现的 CI 路径（Windows + CMake + `ctest`）。
    - CI 默认跑稳定集合（不含 perf/长时间 soak），perf/soak 作为显式触发。

### M2（可玩闭环）

> 原则：M2 不追求“内容多”，只追求“路径短、稳定、可复盘”。

#### P0

- **I-101**：定义并固化“可玩闭环”验收口径（自动化 + 人工）
  - 范围：`tests`、`docs/status`
  - 状态：TODO
  - 验收标准：
    - 自动化用例：闭环流程有端到端覆盖（输入→动作→资源→制作→Boss/目标→存档回读）。
    - 人工清单：包含流程可达、关键交互手感、失败提示与恢复路径（断线/重连/存档异常）等条目。

- **I-102**：正式输入与调试输入必须隔离，且调试输入默认不影响玩家体验
  - 范围：`platform`、`app`
  - 状态：TODO
  - 验收标准：
    - 调试输入具备单一开关（默认关闭），并且不改变正式输入语义。
    - 输入语义文档与实现对齐（见 `docs/architecture/input-and-debug-controls.md`）。

- **I-103**：可玩闭环的“最小路径”必须写成可执行脚本/用例，而不是口头描述
  - 范围：`tests`
  - 状态：TODO
  - 验收标准：
    - 用例能在无窗口环境跑通（不依赖 SDL），并输出清晰的 PASS/FAIL 证据。
    - 用例覆盖至少：资源采集 → 建造/制作门槛 → 目标达成 → 存档回读继续可玩。

#### P1

- **I-111**：存档与 Mod 指纹策略在可玩闭环中形成明确规则
  - 范围：`save`、`mod`
  - 状态：TODO
  - 验收标准：
    - 指纹不一致时的行为（拒绝/警告/降级）有清晰口径与可测试覆盖。
    - 存档内容包含闭环必需状态，回读后可继续闭环而不破坏一致性。
