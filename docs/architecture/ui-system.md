# UI 系统与渲染边界

## 目标

- UI 必须可组合、可测试、可替换，不再在渲染构建器中堆“矩形艺术”。
- UI 必须严格对齐输入语义与玩法契约：Action/Interaction、热栏行切换、智能上下文槽、背包与制作等。
- UI 必须只做“表现层”：不回查世界、不做规则判定，避免跨层耦合导致回归不可控。

## 分层与数据流

### 1) `platform`：只渲染

- 输入：产出 `platform::InputActions`。
- 渲染：消费 `platform::RenderScene`，绘制 tile 与 overlay commands。
- 约束：渲染阶段不得回查 `world`/`sim`。

### 2) `app`：编排与 Model 组装

- 把输入映射为 `app::PlayerInputIntent`。
- 从仿真与本地状态组装稳定 UI 数据：
  - HUD：生命、资源计数、热栏行/槽、智能模式、拾取反馈等。
  - World overlay：目标高亮、可达性、主动作进度等。
  - Inventory overlay：配方选择、配方可用性、站点可达门槛等。
- 产出 `ui::GameplayUiModel`（或拆分为 `HudModel/InventoryModel`），作为 UI 唯一输入。

### 3) `ui`：纯表现层

- 输入：`ui::GameplayUiModel` + `ui::GameplayUiFrameContext` + `ui::GameplayUiPalette`。
- 输出：追加 `platform::RenderCommand` 到 `RenderScene.overlay_commands`。
- 形态建议：
  - `AppendGameplayUi(scene, frame, palette, model)`：纯函数式渲染。
  - 如需状态（滚动、动画队列、焦点），显式传入/返回 `UiState`，可 Reset/可序列化（按需）。

## 关键约束（强约束）

- **UI 不做规则**：配方是否可制作、是否需要工作台、工作台是否在范围内等，必须由上层写入 `UiModel`。
- **命令不可反向流入**：UI 不直接提交 sim 命令；UI 只通过 `UiEvents`（可选）表达“用户想做什么”，由 `app` 负责翻译为玩法命令。
- **确定性**：同一帧 `UiModel` 必须产生一致的覆盖层输出，便于录制/回放/快照测试。
- **像素对齐**：UI 坐标尽量取整，避免渲染抖动与文字发虚。

## 渲染能力与演进

当前 overlay 渲染能力：

- `FilledRect` / `Line` / `Text`

推荐演进（为资源管线与可视化上限服务）：

- 新增 `Sprite`：图标、按钮、物品卡片、热栏槽位等需要稳定贴图输出。
- 新增 `NineSlice`：面板皮肤与边框，避免 UI 永远像调试窗口。
- 字体资源化：字体作为资源输入，避免写死或依赖平台默认字体。

