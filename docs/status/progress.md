# Novaria Progress

最后更新：2026-02-09

## Status

- 当前里程碑：**M2 可玩闭环**（M1 已收口；当前关键待办见 I-026/I-101/I-102）。
- 重构决策（已确认）：硬切 wire v1（二进制）与存档版本；删除所有系统依赖兜底与兼容层。
- 重构决策（已确认，新增）：渲染管线硬切为“`app` 产出自包含 `RenderScene`（最终颜色 + `overlay_commands`），`platform` 只渲染不理解 world/material 语义”；同时 public header 只保留契约，实现型 runtime/backends 头文件下沉到 `src/**` 并通过 PIMPL 隔离。
- 重构决策（已确认，落地中）：玩法权威硬切回 `sim`：`app` 只提交“意图/请求”（primary action、craft recipe 等），禁止直接改库存/规则/世界；库存与制作结算由 `sim` 维护并可被测试覆盖。
- 主要风险（会阻断联调/演进）：
  - **仅剩人工验收**：M2 的人工闭环体验清单尚未确认（见 I-101）。
  - **新增人工验收**：移动/重力/碰撞手感与连续性（见 I-026）。
  - **新增体验阻断**：HUD 视觉语义过于抽象，玩家难以理解当前可操作状态（见 I-102）。
  - **新增演进风险**：HUD 构建与世界渲染逻辑耦合，且 UI 绘制存在可预见性能债（见 I-103/I-104）。
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

- **I-102**：HUD 视觉语义抽象，关键状态“看不懂”
  - 范围：`app`、`platform`、`docs/status`、`tests`
  - 状态：TODO
  - 现象：当前 HUD 主要靠色块表达语义，缺少直观标签与层级；玩家难以在首次进入时判断“我现在能做什么、该按什么、进度到哪了”。
  - 进展：问题已确认并进入 M2；下一步先形成统一视觉语义（信息层级、控件命名、状态标签）再落地实现。
  - 证据：`src/app/render_scene_builder.cpp`、`src/app/player_controller.cpp`、`docs/gameplay/blueprint.md`
  - 验收标准：
    - 新玩家首次进入 60 秒内，可在无外部说明下读懂：生命、资源、当前热栏行/槽、智能模式与上下文槽状态、背包入口。
    - 关键状态必须“文本 + 视觉”双通道表达，不允许只靠颜色编码。
    - 人工步骤（建议 3 分钟）：运行 `build/Debug/novaria.exe`，仅凭 HUD 完成“采集木石→打开背包→选择配方→制作”的最短路径，不依赖口头指引。

#### P1

- **I-103**：HUD 构建职责过重，世界渲染与 UI 语义耦合
  - 范围：`app`、`platform`、`tests`
  - 状态：TODO
  - 现象：`RenderSceneBuilder` 同时负责世界光照计算、HUD 布局、背包绘制与交互反馈，职责边界混杂，改 HUD 容易误伤世界渲染。
  - 进展：已确认需拆分为“HUD 数据模型 / 布局 / 绘制”三层，`platform` 继续只做命令消费。
  - 证据：`src/app/render_scene_builder.cpp`、`include/novaria/app/render_scene_builder.h`、`src/platform/sdl_context.cpp`
  - 验收标准：
    - `RenderSceneBuilder` 不再内聚 HUD 细节算法，HUD 由独立构建器输出 UI 命令。
    - HUD 文案与布局参数从渲染细节中抽离，支持独立单测。
    - 调整 HUD 布局时，不需要改动世界 tile/light 计算逻辑。

- **I-104**：UI 绘制存在性能热点（每帧排序 + 像素字形重绘）
  - 范围：`platform`、`app`、`tests`
  - 状态：TODO
  - 现象：`overlay_commands` 每帧全量排序，文本绘制走逐像素方块路径，HUD 内容扩展后帧时延有放大风险。
  - 进展：问题已确认，待制定最小性能基线并按收益优先级优化（先缓存静态文本，再减少不必要排序）。
  - 证据：`src/platform/sdl_context.cpp`、`src/app/render_scene_builder.cpp`
  - 验收标准：
    - 给出可复现的 UI 绘制基线数据（至少 640x480 与 1920x1080 两档）。
    - 常驻静态文案不再每帧重复生成像素字形。
    - 在同等场景下，UI 绘制耗时较基线显著下降，且 `ctest -C Debug --output-on-failure` 全绿。
