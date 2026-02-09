# Novaria Progress

最后更新：2026-02-09

## Status

- 当前里程碑：**M2 可玩闭环**（M1 已收口；仅剩 I-026/I-101 的人工验收确认）。
- 重构决策（已确认）：硬切 wire v1（二进制）与存档版本；删除所有系统依赖兜底与兼容层。
- 重构决策（已确认，新增）：渲染管线硬切为“`app` 产出自包含 `RenderScene`（最终颜色 + `overlay_commands`），`platform` 只渲染不理解 world/material 语义”；同时 public header 只保留契约，实现型 runtime/backends 头文件下沉到 `src/**` 并通过 PIMPL 隔离。
- 重构决策（已确认，落地中）：玩法权威硬切回 `sim`：`app` 只提交“意图/请求”（primary action、craft recipe 等），禁止直接改库存/规则/世界；库存与制作结算由 `sim` 维护并可被测试覆盖。
- 主要风险（会阻断联调/演进）：
  - **仅剩人工验收**：M2 的人工闭环体验清单尚未确认（见 I-101）。
  - **新增人工验收**：移动/重力/碰撞手感与连续性（见 I-026）。
- 文档分工（强约束）：
  - `docs/architecture/*` 只写**契约与约束**（可作为长期稳定的设计事实源）。
  - `docs/status/*` 只写**当前状态、里程碑、问题单与验收口径**（短期变化频繁的事实源）。
- 工程策略（强约束）：**不建设 CI**。质量门禁以本地可复现命令为准：`cmake -S . -B build`、`cmake --build build --config Debug`、`ctest -C Debug --output-on-failure`；perf 用例需显式开启 `-DNOVARIA_BUILD_PERF_TESTS=ON`。

## Milestones

> 规则：里程碑只表达“阶段目标 + 退出标准”，不描述实现细节；实现细节必须落在 Issue 的验收标准里。

| 编号 | 阶段 | 状态 | 阶段目标 | 退出标准（DoD） |
| --- | --- | --- | --- | --- |
| M1 | 重构（边界与可测性） | DONE | 把依赖方向、模块边界、诊断语义与测试稳定性收口，避免“越做越难改”。 | 1) `app/platform` 不依赖具体 world 实现；2) Net 诊断指标语义一致；3) `ctest -j` 稳定；4) perf 用例不作为默认门禁；5) `PlayerController` 由可替换子组件构成且有回归覆盖。 |
| M2 | 可玩闭环 | 待确认 | 在 M1 基础上，形成可重复验证的最小可玩路径，并给出清晰的人工体验验收清单。 | 1) 自动化：验收/端到端用例通过；2) 人工：闭环流程可达、关键交互手感达标；3) 存档回读不破坏闭环；4) 发布包可复现构建并可启动。 |

## Issues

> 规则：
> 1) Issue 必须可验收：每条都要写“验收标准”；没有验收标准的 Issue 视为未定义。
> 2) `P0`：阻断联调/会导致错误行为/会导致大规模返工；`P1`：近期不修会显著拖慢；`P2`：长期收益但不阻断。
> 3) 状态枚举：`TODO / DOING / 待确认 / DONE / BLOCKED`（默认 `TODO`）。

### M1（重构）

#### P0

- **I-201**：H1（头文件收口）：移除 `include/novaria/**` 转发壳，public/private 头文件物理隔离
  - 范围：`build`、`core`、`net`、`world`、`sim`、`script`、`mod`、`save`、`runtime`、`app`、`platform`、`tools`、`tests`
  - 状态：DONE
  - 验收标准：
    - `include/novaria/**` 中不再出现 `../../../src/**` 这类转发 include。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-202**：B1（依赖策略收口）：删除系统依赖兜底开关，构建策略改为 vendor-only
  - 范围：`build`、`docs/engineering`
  - 状态：DONE
  - 验收标准：
    - CMake 不再暴露 `NOVARIA_ALLOW_SYSTEM_DEPS`，也不再尝试 `find_package` 系统依赖兜底。
    - 缺失 vendor 依赖时配置阶段 fail-fast，错误信息明确指向 `third_party/**` 目录约定。
    - 工程文档不再描述系统依赖兜底与手工路径注入。

- **I-203**：T1（工具收口）：tools 可执行禁止复制编译 `src/**` 业务源文件，必须链接库复用
  - 范围：`build`、`tools`
  - 状态：DONE
  - 验收标准：
    - `novaria_net_smoke/novaria_net_soak` 不再直接编译 `src/net/**.cpp`，改为链接 `novaria_engine`（或后续拆分后的模块库）。

- **I-204**：P1（协议硬切）：落地 Wire Protocol v1（二进制），移除字符串协议与非单射编码
  - 范围：`net`、`world`、`save`、`tests`
  - 状态：DONE
  - 验收标准：
    - `net` datagram 统一 envelope（含 `wire_version/kind/payload_len`），未知 kind/command 丢弃并计数。
    - `WorldSnapshotCodec` 编码迁到 v1（至少 `chunk_snapshot_batch`），禁止 CSV/分隔符拼接。
    - 自动化覆盖：编解码 round-trip、非法输入丢弃、跨主机 smoke 不回退旧协议。

- **I-205**：S1（存档硬切）：存档版本升级并只接受新格式，旧格式直接拒绝
  - 范围：`save`、`runtime`、`tests`
  - 状态：DONE
  - 验收标准：
    - 存档 world 快照字段复用 v1 bytes 表示（可 base64/hex 存储但不改变语义）。
    - 旧存档格式不做迁移/兼容；检测到旧格式 fail-fast 并输出清晰错误。

- **I-001**：UDP “self” 误判导致跨主机不发包
  - 范围：`net`
  - 状态：DONE
  - 现象：当本地绑定 `0.0.0.0` 时，远端端点可能被误判为 self，导致 `SubmitLocalCommand()` 直接跳过发送。
  - 证据：`src/net/net_service_udp_peer.cpp`（`IsSelfEndpoint`、self suppression 路径）。
  - 验收标准：
    - 配置 `bind_host=0.0.0.0` 的跨主机场景下，命令与快照 datagram 能稳定发出并被对端接收。
    - 新增覆盖该场景的自动化验证（单测或工具级 smoke）。

- **I-017**：Mod `script_entry` 路径校验不严（Windows 可绕过为任意盘符路径）
  - 范围：`app`、`tools`、`script`、`mod`
  - 状态：DONE
  - 现象：在 Windows 上形如 `C:foo/bar.lua` 不是 absolute path，但属于带 `root_name` 的路径；当前校验仅禁用 `is_absolute()` 与 `..`，无法阻止读取/执行 mod_root 之外的文件。
  - 进展：补齐 `root_name/root_directory` 禁止与 canonical 路径越界检测，并加入覆盖测试。
  - 证据：`src/runtime/mod_script_loader.cpp:27`、`tests/runtime/mod_script_loader_tests.cpp:92`。
  - 验收标准：
    - `script_entry` 只能指向 `manifest.root_path` 下的文件（join 后 canonical 仍在 root 内）。
    - 针对 `C:...`、`\\server\share`、符号链接等绕过路径新增测试覆盖。

#### P1

- **I-002**：Net 诊断指标语义不一致（“看似 dropped，实际已执行/已入队”）
  - 范围：`net`、`sim`
  - 状态：DONE
  - 现象：断线态/未连接态下本地命令可能仍进入处理队列，但同时计入 dropped 指标，导致诊断误导与门禁误判。
  - 证据：`src/net/net_service_udp_peer.cpp`（`SubmitLocalCommand` dropped/unsent 语义）。
  - 验收标准：
    - `dropped_*` 只统计“实际丢弃（未进入任何处理路径）”。
    - `unsent_* / suppressed_send_*`（或等价命名）用于描述“未发送到远端”的情况。
    - `docs/engineering/release-and-diagnostics.md`（如有）或日志输出口径同步更新。

- **I-025**：C3（产物重组）：public/private 头文件物理隔离 + include 路径收口（固化依赖方向）
  - 范围：`build`、`core`、`net`、`world`、`sim`、`script`、`app`、`platform`、`tools`、`tests`
  - 状态：DONE
  - 现象：目前大多数 target 直接把 `src` 加进 include path，导致任何模块都能随意 include 实现头文件（例如 `world_service_basic.h`），依赖方向无法被编译期约束，边界会持续复发（见 I-003/I-024）。
  - 进展：引入 `include/novaria/**` 公共头目录；`novaria_engine` 采用 `PUBLIC include/novaria + PRIVATE src`；`tools/tests` 切到通过链接库获取 include；`server_main` 与 `tests` 去除对 `world_service_basic.h` 的直接依赖。
  - 验收标准：
    - 引入 `include/novaria/**`（或等价目录）承载**稳定 public headers**；实现细节留在 `src/**` 且不对外暴露 include path。
    - CMake 目标以 `target_include_directories(PUBLIC include, PRIVATE src)` 建模；可执行与测试只通过链接库获得 include，不再全局吃 `src`。
    - 编译期可阻止越界 include（例如 `platform/app` 无法 include `world/world_service_basic.h`），并用一次性迁移消除 legacy include 路径。
    - `cmake --build` 与 `ctest -C Debug` 全量通过。

- **I-018**：Mod 指纹 canonical 编码非单射（字段含分隔符可制造同一输入串）
  - 范围：`mod`
  - 状态：DONE
  - 现象：当前 canonical 字符串使用 `|`, `,`, `:` 等分隔符拼接字段，但字段值本身未做转义/长度前缀；指纹用于一致性校验时存在“编码层碰撞”的风险（换更强 hash 也无济于事）。
  - 证据：`src/mod/mod_loader.cpp:166`（`canonical_entry` 拼接）。
  - 验收标准：
    - canonical 编码改为长度前缀或结构化序列化（保证单射）。
    - 指纹算法升级为加密哈希（与 I-008 可合并或拆分实施）。

- **I-019**：存档替换流程非原子，失败路径可能丢失 `.tmp`（影响可恢复性）
  - 范围：`save`
  - 状态：DONE
  - 现象：写入流程先删 `world.sav` 再 rename `.tmp`，rename 失败会删 `.tmp`；崩溃/锁文件场景下可恢复性差。
  - 证据：`src/save/save_repository.cpp:177`、`src/save/save_repository.cpp:179`、`src/save/save_repository.cpp:182`。
  - 验收标准：
    - rename 失败不删除 `.tmp`，并提供清晰恢复/告警策略。
    - 在 Windows 上优先使用可替换的原子移动（平台特化实现允许）。

- **I-020**：World 初始同步策略缺失（只发 dirty 增量，可能导致新连接端缺地形/缺块）
  - 范围：`world`、`sim`、`net`
  - 状态：DONE
  - 现象：`LoadChunk` 不标脏；网络发布只发送 dirty chunks；新会话/重连时缺少全量/窗口快照同步机制。
  - 证据：`src/world/world_service_basic.cpp:194`（`LoadChunk` 不触发 dirty）、`src/sim/simulation_kernel.cpp:569`（仅消费 dirty 并 publish）。
  - 验收标准：
    - 连接建立后存在明确的初始同步步骤（全量或窗口快照），并有自动化覆盖。
    - 增量 dirty 与初始同步的职责边界清晰（避免用 dirty 假装全量）。

- **I-021**：渲染视口与输入视口不一致（窗口 resize 后 scene 与目标解析可能错位）
  - 范围：`app`、`platform`
  - 状态：DONE
  - 现象：输入侧使用实时 window size 写入 `InputActions.viewport_*`，但渲染场景构建使用静态 `config.window_*` 推导 `view_tiles_*`，导致 resize 后视野/鼠标目标换算不一致。
  - 证据：`src/platform/sdl_context.cpp:284`、`src/app/render_scene_builder.cpp:30`。
  - 验收标准：
    - `RenderSceneBuilder` 使用实时 viewport（或明确禁止 resize 并在平台层锁定）。
    - 有覆盖 resize 的回归用例或可观测诊断。

- **I-022**：Lua 沙箱策略仍偏“删全局”，能力面最小化不足
  - 范围：`script`
  - 状态：DONE
  - 现象：`openlibs` 后再清除若干全局，仍可能保留逃逸面；能力白名单与 `_ENV` 构建策略未系统化。
  - 证据：`src/script/lua_jit_script_host.cpp:352`（`ApplyMvpSandbox`）。
  - 进展：改为白名单构建全局环境并替换 `_G`，保留必要 stdlib，移除 `string.dump`。
  - 验收标准：
    - 明确威胁模型与能力白名单（与 `docs/engineering/mod-compatibility-matrix.md` 对齐）。
    - 以“白名单构建环境”为主，而不是“黑名单删除”。
    - 新增针对逃逸面/禁用项的回归测试。

- **I-026**：连续角色运动与碰撞形状（为半砖/斜坡奠基）
  - 范围：`app`、`sim`、`world`、`platform`、`tests`
  - 状态：待确认
  - 现象：当前移动/重力呈离散跳变，难以支持半砖/斜坡等连续地形形状。
  - 进展：引入 `sim/player_motion` 连续物理与碰撞采样；`world/material_catalog` 增加 `CollisionShape`；渲染相机/角色位置改为浮点并跟随运动。
  - 证据：`src/sim/player_motion.cpp`、`src/world/material_catalog.h`、`src/platform/render_scene.h`、`src/app/player_controller.cpp`、`tests/mvp/gameplay_issue_e2e_tests.cpp`
  - 验收标准：
    - 移动/重力连续，无明显“像素卡顿”（人工验收）。
    - 人工步骤（建议 2 分钟）：运行 `build/Debug/novaria.exe`，按住 `A/D` 来回移动 + `Space` 连跳；贴墙/顶到方块移动不抖动、不穿透、不“弹飞”。
    - 碰撞形状支持 Full/Half/Slope 的基础表达，并被运动系统消费。
    - `ctest -C Debug --output-on-failure` 全绿。
- **I-024**：B（结构债）：边界/职责/权威收口（Epic）
  - 范围：`app`、`sim`、`world`、`platform`、`script`
  - 状态：DONE
  - 说明：把“上帝类 + 规则复制 + world 泄漏 + Lua 侵入式胶水”收敛为一条可验收的重构主线，避免各处零敲碎打造成二次返工。
  - 进展：I-003/I-004/I-005/I-006/I-014/I-010/I-022 全部收口，M1 阶段结构债主线关闭。
  - 依赖：I-003、I-004、I-005、I-006、I-014、I-010、I-022
  - 验收标准：
    - 依赖方向清晰：`platform/app` 不包含 world 实现细节；材料/渲染元信息走稳定接口。
    - “玩法门槛/成本”只有一个权威来源（见 I-005），UI 不再自算门槛。
    - `PlayerController` 仅编排，核心行为被可单测组件承载（见 I-004）。
    - `SimulationKernel` 仅调度，规则落在 ruleset/system（见 I-006）。
    - Lua bootstrap 外置且可审计；沙箱能力面按白名单最小化推进（见 I-010/I-022）。

- **I-003**：`world` 实现细节泄漏到 `app/platform`，模块边界失效
  - 范围：`world`、`app`、`platform`
  - 状态：DONE
  - 现象：上层直接依赖 `WorldServiceBasic::kMaterial*` 等实现细节，导致 world 替换与演进成本极高。
  - 进展：引入 `world::IWorldService` 的稳定读取面（`TryReadTile/LoadedChunkCoords`）与 `world/material_catalog`，并推动 `app/platform/tools/tests` 去依赖实现头文件。
  - 证据：`src/world/world_service.h:20`、`src/world/material_catalog.h:1`、`src/platform/sdl_context.cpp:4`、`src/app/player_controller.h:5`、`src/app/render_scene_builder.h:6`。
  - 验收标准：
    - `platform` 不包含任何 world 具体实现头文件。
    - `app` 只依赖 `world::IWorldService`（以及稳定的材料/渲染元信息接口），不依赖 `WorldServiceBasic` 常量。
    - 材料定义从 world 实现中抽离为稳定概念（例如 `MaterialId` + traits/catalog）。

- **I-004**：`PlayerController` 上帝类拆分与表驱动
  - 范围：`app`
  - 状态：DONE
  - 现象：输入、状态机、chunk 窗口、采集/放置/交互/制作、HUD 状态等混杂在一个函数里，分支爆炸且难以测试。
  - 进展：新增 hotbar action 表驱动解析与组件级测试，`Update()` 仅编排调用与状态推进。
  - 证据：`src/app/player_controller_components.cpp:48`、`src/app/player_controller.cpp:255`、`tests/app/player_controller_components_tests.cpp:190`。
  - 验收标准：
    - `PlayerController` 变为“编排器”，核心行为分解为可替换/可单测的子组件（至少：目标解析、动作执行、背包/热栏、智能选择、chunk 窗口）。
    - 热栏/工具/动作分支由数据结构或函数表驱动，显著减少 if/else 嵌套。
    - 现有端到端用例不回退，且新增单元级覆盖（不依赖 SDL）。

- **I-005**：玩法参数与门槛存在重复定义，可能产生“UI 认为可做但内核拒绝”的撕裂
  - 范围：`app`、`sim`
  - 状态：DONE
  - 进展：玩法成本/门槛统一到 `sim/gameplay_balance.h`，并由 `GameplayProgressSnapshot` 驱动 UI 关键状态展示。
  - 证据：`src/sim/gameplay_balance.h:1`、`src/app/player_controller.cpp:45`、`src/sim/gameplay_ruleset.cpp:176`。
  - 验收标准：
    - 玩法门槛/成本只存在一个权威来源（建议在 `sim` 或数据驱动层），UI 仅展示权威状态与提示。

- **I-006**：`SimulationKernel` 混合“调度 + 玩法规则 + 文本事件拼装”，演进风险高
  - 范围：`sim`
  - 状态：DONE
  - 进展：玩法进度/规则与事件派发从 `SimulationKernel` 拆入 `GameplayRuleset`，Kernel 回归“调度与跨模块协调”。
  - 证据：`src/sim/gameplay_ruleset.h:1`、`src/sim/simulation_kernel.cpp:1`。
  - 验收标准：
    - `SimulationKernel` 保持“调度与跨模块协调”的职责边界。
    - 玩法规则独立为可测试的 system / ruleset（可先做最小拆分，不要求一步到位数据驱动）。

- **I-014**：`GameApp` 装配层过度集中（启动/存档/模组/脚本/主循环绑死），难以复用与测试
  - 范围：`app`
  - 状态：DONE
  - 证据：`src/app/game_app.h:34`（聚合所有服务）、`src/app/game_app.cpp`（Initialize 同时做 config/save/mod/script 逻辑）。
  - 进展：装配拆分为 runtime 管线与策略组件（mod pipeline / save loader / fingerprint policy），`GameApp` 仅编排调用。
  - 证据：`src/runtime/mod_pipeline.h:1`、`src/runtime/save_state_loader.h:1`、`src/runtime/mod_fingerprint_policy.h:1`。
  - 验收标准：
    - 把“装配/加载”拆为独立组件（例如 config/bootstrap/save/mod/script 各自模块化）。
    - `GameApp` 只保留编排与生命周期，不再承载具体加载细节。

- **I-012**：测试临时目录使用固定名称，`ctest -j` 并行会互相删除导致随机失败
  - 范围：`tests`
  - 状态：DONE
  - 进展：所有落盘测试目录改为唯一名称（引入高精度时间种子），避免并行互删。
  - 证据：`tests/core/config_tests.cpp:19`、`tests/mod/mod_loader_tests.cpp:20`、`tests/save/save_repository_tests.cpp:18`、`tests/sim/simulation_kernel_tests.cpp:137`。
  - 验收标准：
    - 所有落盘测试目录均为唯一（至少包含 pid 或随机后缀）。
    - `ctest -j`（并行）在本机稳定通过。

- **I-013**：perf 用例作为默认门禁（Debug/不同机器抖动），降低研发效率
  - 范围：`tests`、`engineering`
  - 状态：DONE
  - 进展：引入 `NOVARIA_BUILD_PERF_TESTS` 开关，将 perf 用例从默认 `ctest` 集合中隔离，并用 label 标注。
  - 证据：`tests/mvp/mvp_acceptance_tests.cpp:316`（硬阈值 p95 ≤ 16.6ms）。
  - 验收标准：
    - perf 用例从默认 `ctest` 运行集中隔离（label/单独 target/显式开关）。
    - perf 只在约定构建配置（例如 `RelWithDebInfo`）下作为门禁。

#### P2

- **I-023**：工程文档的输入说明与实现漂移（会误导验收与回归定位）
  - 范围：`docs`、`platform`
  - 状态：DONE
  - 现象：工程文档仍提及旧输入语义/调试键位，但实现中已不匹配；文档作为事实源时会直接误导测试与验收。
  - 证据：`docs/engineering/build-and-run.md:127`、`src/platform/sdl_context.cpp:201`。
  - 验收标准：
    - `docs/architecture/input-and-debug-controls.md` 作为唯一输入契约事实源；工程文档只引用链接，不重复列语义。
    - 调试输入必须有单一开关与可审计输出，并在文档中明确默认值。

- **I-007**：配置文件名为 `.toml` 但解析器不是 TOML 子集，易产生“写了但没生效”
  - 范围：`core/config`
  - 状态：DONE
  - 进展：采用路径 B：默认配置更名为 `config/game.cfg`，并落地 unknown key 告警 + `strict_config_keys` 严格模式。
  - 证据：`src/core/config.cpp`、`config/game.cfg`、`src/main.cpp`、`tools/server_main.cpp`、`docs/engineering/build-and-run.md`。
  - 验收标准（两条路径二选一）：
    - A) 引入真正的 TOML 解析并定义兼容子集；或
    - B) 明确更名并增加 unknown key 告警/严格模式。

- **I-008**：Mod 指纹用于一致性校验但采用 64-bit FNV，碰撞风险不可控
  - 范围：`mod`
  - 状态：DONE
  - 进展：指纹升级为 SHA-256（注意：编码单射问题仍需 I-018 解决；强 hash 不等于可证明的 canonical）。
  - 证据：`src/mod/mod_loader.cpp:211`、`src/core/sha256.h:1`。
  - 验收标准：
    - 指纹升级为加密哈希（建议 SHA-256），并把其视为协议字段管理版本迁移。

- **I-009**：Mod 脚本装载辅助逻辑在 `game_app` 与 `server_main` 复制，易产生行为漂移
  - 范围：`app`、`tools`
  - 状态：DONE
  - 进展：提取为 `runtime/mod_script_loader.*`，`game_app` 与 `server_main` 复用同一实现。
  - 证据：`src/runtime/mod_script_loader.h:1`、`src/app/game_app.cpp:6`、`tools/server_main.cpp:214`。
  - 验收标准：
    - 提取为单一实现（例如 `src/runtime/mod_script_loader.*`），两端共享。

- **I-010**：Lua bootstrap 脚本内嵌在 `.cpp` 中，不利于迭代与审计
  - 范围：`script`
  - 状态：DONE
  - 进展：bootstrap 外置为 `bootstrap.lua` 并在构建期嵌入；开发态可用 `NOVARIA_LUA_BOOTSTRAP_FILE` 覆盖加载源以加速迭代。
  - 证据：`src/script/bootstrap.lua:1`、`src/script/lua_jit_script_host.cpp:24`、`CMakeLists.txt:15`。
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
  - 状态：DONE
  - 进展：抽为 `novaria_engine` 静态库，`novaria/novaria_server` 仅链接，避免重复编译。
  - 证据：`CMakeLists.txt:173`、`CMakeLists.txt:232`、`CMakeLists.txt:262`。
  - 验收标准：
    - 模块拆分为库（static/object），可执行文件只链接，避免重复编译。

### M2（可玩闭环）

> 原则：M2 不追求“内容多”，只追求“路径短、稳定、可复盘”。

#### P0

- **I-101**：定义并固化“可玩闭环”验收口径（自动化 + 人工）
  - 范围：`tests`、`docs/status`
  - 状态：待确认
  - 进展：自动化闭环已落地（`novaria_gameplay_issue_e2e_tests` + `novaria_mvp_acceptance_tests`）；人工体验清单需项目负责人执行并确认后才可标 DONE。
  - 验收标准：
    - 自动化用例：闭环流程有端到端覆盖（输入→动作→资源→制作→Boss/目标→存档回读）。
    - 人工清单（建议 5 分钟，项目负责人确认后才可标 DONE）：
      - 启动 `build/Debug/novaria.exe`，确认可移动（`A/D`）与跳跃（`Space`）正常。
      - 采集与拾取：选 `1`（镐）挖石、选 `2`（斧）砍木；左键持续动作；材料掉落后可拾取，HUD 计数上升。
      - 制作：`Esc` 打开背包，按 `2` 选择木剑配方，右键交互完成制作；回到主界面后选 `7`（木剑）。
      - 目标：使用木剑推动“可玩闭环完成”（Boss/目标达成的提示或 HUD 状态发生变化）。
      - 存档回读：退出程序再重启，闭环关键状态与资源计数不丢失（至少包含：工作台/木剑/目标完成的任意组合信号）。

- **I-104**：权威玩法收口到 `sim`（硬切：`app` 禁止直接结算）
  - 范围：`app`、`sim`、`world`、`tests`
  - 状态：DONE
  - 说明：把“采集/放置/制作/拾取结算”从 `PlayerController` 硬切回 `SimulationKernel` 路径，`app` 仅提交意图命令并从 `sim` 读取权威库存快照用于 HUD。
  - 证据：`src/sim/simulation_kernel.cpp`（inventory + craft/action 结算）、`src/app/player_controller.cpp`（仅提交意图 + 读取库存快照）、`docs/architecture/wire-protocol.md`
  - 验收标准：
    - `app` 不再直接修改库存计数，不再直接提交 `world.set_tile + spawn_drop` 来表达采集/放置的最终结果。
    - `sim` 维护权威库存快照，并在 `ctest -C Debug --output-on-failure` 全绿时可证明“采集→掉落→拾取→制作→放置”的闭环不回退。
    - `novaria_gameplay_issue_e2e_tests` 的“工具门禁/可达门禁/工作台距离门禁/火把放置与光照”覆盖不回退。

- **I-105**：强制 `core` Mod 与 core script（无 Mod 直接 fail-fast，不做兜底）
  - 范围：`mods`、`runtime`、`tools/release`、`docs/engineering`
  - 状态：DONE
  - 说明：MVP 阶段不允许“Lua/Mod 可有可无”；缺失 `mods/core` 或 `core` 无脚本入口时直接启动失败，避免内容面与脚本面长期空转造成结构债。
  - 证据：`mods/core/mod.cfg`、`mods/core/content/scripts/core.lua`、`src/runtime/mod_pipeline.cpp`、`tools/release/build_release.ps1`
  - 验收标准：
    - 启动装配阶段缺失 `mods/core/mod.cfg` 或缺失 `script_entry` 时 fail-fast，错误信息明确。
    - release 打包包含 `mods/core`。
    - `ctest -C Debug --output-on-failure` 全绿。

- **I-106**：玩家运动与位置权威化（EnTT），协议移除 `player_tile_x/y`
  - 范围：`sim`、`app`、`tests`、`docs/architecture`
  - 状态：DONE
  - 说明：`gameplay.action_primary`/`gameplay.craft_recipe` 不再由客户端上报玩家 tile 坐标；`sim::ecs::Runtime` 持有玩家 `PlayerMotion` 并用于距离判定与工作台可达性；`SimulationKernel` 不再维护本地玩家运动状态。
  - 证据：`include/novaria/sim/command_schema.h`、`src/sim/command_schema.cpp`、`include/novaria/sim/ecs_runtime.h`、`src/sim/ecs_runtime.cpp`、`include/novaria/sim/simulation_kernel.h`、`src/sim/simulation_kernel.cpp`、`docs/architecture/wire-protocol.md`
  - 验收标准：
    - wire v1 中 `action_primary/craft_recipe` payload 不包含 `player_tile_x/y`，且 `docs/architecture/wire-protocol.md` 同步。
    - `SimulationKernel::LocalPlayerMotion` 返回值来自 `sim::ecs::Runtime`（不再持有本地运动 state）。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-107**：输入侧链硬切：玩家运动输入也必须走 command（wire v1）
  - 范围：`app`、`sim`、`docs/architecture`、`tests`
  - 状态：DONE
  - 说明：移除 `SimulationKernel::SetLocalPlayerMotionInput`；`PlayerController` 每 tick 提交 `player.motion_input`，权威侧在 `SimulationKernel::Update` 中执行并驱动 `sim::ecs::Runtime` 更新玩家运动。
  - 证据：`include/novaria/sim/command_schema.h`、`src/sim/command_schema.cpp`、`include/novaria/sim/typed_command.h`、`src/sim/typed_command.cpp`、`include/novaria/sim/simulation_kernel.h`、`src/sim/simulation_kernel.cpp`、`src/app/player_controller.cpp`、`docs/architecture/wire-protocol.md`
  - 验收标准：
    - wire v1 `command_id=3` 固定为 `player.motion_input`，payload 定义在 `docs/architecture/wire-protocol.md`。
    - `PlayerController` 不再通过 setter 注入运动输入，只通过 `SubmitLocalCommand`。
    - `ctest -C Debug --output-on-failure` 全绿。

- **I-108**：玩法规则 Lua 化（core script RPC / simrpc v1）：`action_primary`/`craft_recipe` 的“计划解析”交给 Lua
  - 范围：`script`、`sim`、`mods/core`、`tests`
  - 状态：DONE
  - 说明：`sim` 在执行 `gameplay.action_primary` / `gameplay.craft_recipe` 时同步调用 `core` 脚本函数 `novaria_on_sim_command`，由 Lua 返回可执行“计划”（harvest/place/craft 的参数与资源消耗/产出）；C++ 仅负责权威执行与物理/世界写入（无兼容与兜底）。RPC 载荷采用结构化二进制 schema（simrpc v1），不再使用字符串 KV 协议。
  - 证据：`include/novaria/script/script_host.h`、`include/novaria/script/sim_rules_rpc.h`、`src/script/lua_jit_script_host.cpp`、`src/sim/simulation_kernel.cpp`、`include/novaria/sim/ecs_runtime.h`、`src/sim/ecs_runtime.cpp`、`mods/core/content/scripts/core.lua`
  - 验收标准：
    - 启动初始化阶段对 `core` 脚本进行 validate RPC（缺失/失败即初始化失败）。
    - `gameplay.action_primary` / `gameplay.craft_recipe` 的决策不再硬编码在 C++，由 `core.lua` 返回计划驱动。
    - `core` RPC 的 request/response 采用 `include/novaria/script/sim_rules_rpc.h` 定义的二进制 schema（version=1），不再解析/生成 KV 文本载荷。
    - `ctest -C Debug --output-on-failure` 全绿。

- **I-301**：R0（渲染边界硬切）：`platform` 禁止依赖 `world/*`，渲染输入必须自包含
  - 范围：`platform`、`app`
  - 状态：DONE
  - 验收标准：
    - `src/platform/**` 不再 include `world/**`，不再通过 material_id 调用 world 的颜色/语义函数。
    - `platform::RenderScene` 只包含绘制所需的最终数据（tile 颜色 + `overlay_commands`），不暴露 HUD/菜单语义，`platform` 只做绘制。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-302**：H2（public header 收口）：runtime/backends 采用 PIMPL，移除 public 对实现头文件的包含
  - 范围：`net`、`script`、`build`、`tests`
  - 状态：DONE
  - 验收标准：
    - `include/novaria/net/net_service_runtime.h` 不再 include 任何具体 backend（例如 `net_service_udp_peer`）。
    - `include/novaria/script/script_host_runtime.h` 不再 include 任何具体 backend（例如 LuaJIT host）。
    - 仍可运行并通过相应单测（`novaria_net_service_runtime_tests`、`novaria_script_host_runtime_tests`）。

- **I-303**：H3（实现头文件下沉）：实现型 backends 头文件移动到 `src/**`，仅测试与实现可见
  - 范围：`net`、`script`、`build`、`tests`
  - 状态：DONE
  - 验收标准：
    - `net_service_udp_peer.h`、`lua_jit_script_host.h` 等实现头文件不再位于 `include/novaria/**`。
    - 仅需要实现细节的测试 target 额外 include `src`，其余 target 仍只 include `include/novaria`。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-304**：N1（命名硬切）：`udp_loopback` 更名为 `udp_peer`，并同步收口实现与测试命名
  - 范围：`core/config`、`net`、`tools`、`tests`、`docs/engineering`
  - 状态：DONE
  - 验收标准：
    - 配置只接受 `net_backend="udp_peer"`，不再接受旧字符串。
    - 具体实现与测试命名同步更正（`NetServiceUdpPeer`、`novaria_net_service_udp_peer_tests`）。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-305**：N2（清单更名）：Mod 清单从 `mod.toml` 硬切为 `mod.cfg`（不做兼容）
  - 范围：`mod`、`runtime`、`tests`、`docs/engineering`、`docs/architecture`
  - 状态：DONE
  - 验收标准：
    - 扫描并解析 `mods/<mod_name>/mod.cfg`；`mod.toml` 不再被识别或兜底。
    - 所有测试与工程文档示例同步更名为 `mod.cfg`。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-306**：R1（UI 绘制收口）：`platform` 只消费绘制命令，不承载 HUD/菜单布局决策
  - 范围：`app`、`platform`
  - 状态：DONE
  - 验收标准：
    - `platform::SdlContext::RenderFrame` 不再计算 HUD/菜单布局，仅绘制 `RenderTile` 与 `overlay_commands`（Rect/Line/Text）。
    - HUD/菜单/选择器/提示条等 UI 的绘制命令由 `app::RenderSceneBuilder` 生成并塞入 `platform::RenderScene::overlay_commands`。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-307**：C1（配置解析收口）：`config` 与 `mod.cfg` 复用同一 cfg 解析器
  - 范围：`core`、`mod`
  - 状态：DONE
  - 验收标准：
    - 新增 `core::cfg::ParseFile` 作为单一 key-value 事实解析入口（支持注释、section、quoted string、bool、int、string array）。
    - `core::ConfigLoader` 与 `mod::ModLoader` 不再维护各自的行级 trim/注释剥离/数组拆分逻辑。
    - `cmake --build build --config Debug` 与 `ctest -C Debug --output-on-failure` 通过。

- **I-102**：正式输入与调试输入必须隔离，且调试输入默认不影响玩家体验
  - 范围：`platform`、`app`
  - 状态：DONE
  - 验收标准：
    - 调试输入具备单一开关（默认关闭），并且不改变正式输入语义。
    - 输入语义文档与实现对齐（见 `docs/architecture/input-and-debug-controls.md`）。

- **I-103**：可玩闭环的“最小路径”必须写成可执行脚本/用例，而不是口头描述
  - 范围：`tests`
  - 状态：DONE
  - 验收标准：
    - 用例能在无窗口环境跑通（不依赖 SDL），并输出清晰的 PASS/FAIL 证据。
    - 用例覆盖至少：资源采集 → 建造/制作门槛 → 目标达成 → 存档回读继续可玩。

#### P1

- **I-111**：存档与 Mod 指纹策略在可玩闭环中形成明确规则
  - 范围：`save`、`mod`
  - 状态：DONE
  - 进展：指纹策略封装为 runtime policy，并补齐 mismatch 决策测试覆盖。
  - 证据：`src/runtime/mod_fingerprint_policy.h:1`、`tests/runtime/mod_fingerprint_policy_tests.cpp:1`。
  - 验收标准：
    - 指纹不一致时的行为（拒绝/警告/降级）有清晰口径与可测试覆盖。
    - 存档内容包含闭环必需状态，回读后可继续闭环而不破坏一致性。
