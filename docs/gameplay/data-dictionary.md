# Novaria Gameplay Data Dictionary

## 1. 材质定义

| Material ID | Key | 语义 | 是否可站立 | 是否阻挡太阳光 | 是否可采集 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | `air` | 空气 | 否 | 否 | 否 | 默认空格 |
| 1 | `dirt` | 土层基础块 | 是 | 是 | 是 | 可放置 |
| 2 | `stone` | 石层基础块 | 是 | 是 | 是 | 可放置 |
| 3 | `grass` | 地表覆盖层 | 是 | 是 | 是 | 采集后回收为土资源 |
| 4 | `water` | 静态水体 | 否 | 否 | 否 | 不流动 |
| 5 | `wood` | 树干材质 | 是 | 是 | 是 | 产出木材 |
| 6 | `leaves` | 树冠材质 | 否 | 否 | 是 | 可破坏 |
| 7 | `coal_ore` | 煤矿材质 | 是 | 是 | 是 | 产出煤 |
| 8 | `torch` | 火把实体格 | 否 | 否 | 是 | 提供局部光照 |

## 2. 资源、工具与物品

| Key | 类型 | 来源/获得方式 | 用途 |
| --- | --- | --- | --- |
| `resource.dirt` | 资源 | 采集 `dirt/grass` | 地形放置 |
| `resource.stone` | 资源 | 采集 `stone` | 地形放置、配方消耗 |
| `resource.wood` | 资源 | 采集 `wood` | 配方消耗 |
| `resource.coal` | 资源 | 采集 `coal_ore` | 火把配方消耗 |
| `tool.pickaxe_t0` | 工具（可挥舞） | 开局发放 | 采矿、采石、采集地表硬块 |
| `tool.axe_t0` | 工具（可挥舞） | 开局发放 | 砍树、砍叶 |
| `item.workbench` | 站点状态 | 配方产出 | 工作台配方前置 |
| `item.wood_sword` | 武器（可挥舞） | 配方产出 | 近战攻击、低效率采掘 |
| `item.torch` | 可放置物品 | 配方产出 | 夜间照明 |

## 3. 开局载荷

| Key | 默认值 | 说明 |
| --- | --- | --- |
| `starter.tool_pickaxe_t0` | `1` | 防止开局采矿软锁 |
| `starter.tool_axe_t0` | `1` | 防止开局砍树软锁 |

## 4. 配方定义（无形状）

| Recipe Key | 前置条件 | 消耗 | 产出 |
| --- | --- | --- | --- |
| `recipe.workbench` | 无 | `wood >= 10` | `item.workbench = 1` |
| `recipe.wood_sword` | `in_range(workbench, 4)` | `wood >= 7` | `item.wood_sword = 1` |
| `recipe.torch` | 无 | `wood >= 1` 且 `coal >= 1` | `item.torch += 4` |

## 5. 输入字典

| Input | Action/Interaction Key | 语义 |
| --- | --- | --- |
| `A/D` | `move.horizontal` | 水平移动 |
| `Space` | `move.jump` | 跳跃 |
| 鼠标左键（按住） | `action.primary` | 持续主动作 |
| 鼠标右键（按下） | `interaction.primary` | 瞬时交互 |
| 滚轮/`1-0` | `hotbar.select_slot` | 切槽 |
| `Esc` | `ui.inventory.toggle` | 背包与制作界面开关 |
| `Tab` | `hotbar.select_next_row` | 切换到下一行热栏 |
| `W`/`↑` | `ui.nav.up` | UI 向上导航（配方/列表） |
| `S`/`↓` | `ui.nav.down` | UI 向下导航（配方/列表） |
| `Enter` | `ui.nav.confirm` | UI 确认/执行（制作/选择） |
| `Ctrl` | `smart_mode.toggle` | 智能选择模式开关 |
| `Shift`（按住） | `smart_context.hold` | 高亮目标与上下文槽 |

## 6. 行为抽象

### 6.1 Action（持续）

| Key | 输入 | 执行模式 | 结束条件 |
| --- | --- | --- | --- |
| `action.primary` | 左键按住 | 每 tick 组装并执行 `ActionBundle` | 松开按键或目标失效 |

### 6.2 ActionBundle（组合）

| Bundle Key | 组成 Action | 触发条件 | 说明 |
| --- | --- | --- | --- |
| `bundle.harvest` | `Swing + Harvest` | 目标为可采集方块且工具匹配 | 采集路径 |
| `bundle.place` | `PlaceBlock` | 手持可放置物且目标为 `air` | 放置路径 |
| `bundle.combat_ranged` | `Aim + ProjectileFire` | 远程武器启用后 | 预留，不在当前阶段启用 |

### 6.3 Interaction（瞬时）

| Key | 输入 | 执行模式 | 结束条件 |
| --- | --- | --- | --- |
| `interaction.primary` | 右键按下 | 单次触发 | 当帧结束 |

### 6.4 InteractionType

| Type | 语义 |
| --- | --- |
| `OpenContainer` | 打开容器界面 |
| `OpenCrafting` | 打开制作界面 |
| `UseStation` | 使用工作站 |
| `OfferItem` | 献祭或提交物品 |
| `ActivateObject` | 激活场景对象 |
| `TalkNpc` | 触发 NPC 对话 |

## 7. Action 执行契约（采集/放置分离）

### 7.1 采集与放置规则

| Rule Key | Action | 目标 | 必要条件 | 结果 |
| --- | --- | --- | --- | --- |
| `action.harvest.mine` | `Harvest` | `stone / coal_ore / dirt / grass` | 手持 `tool.pickaxe_t0+` | 方块被采集并生成对应掉落 |
| `action.harvest.chop` | `Harvest` | `wood / leaves` | 手持 `tool.axe_t0+` | 方块被采集并生成对应掉落 |
| `action.place.block` | `PlaceBlock` | `air` | 手持对应可放置物品且通过可达校验 | 在目标空格放置对应方块 |
| `action.place.torch` | `PlaceBlock` | `air` | 手持 `item.torch` 且目标非水体 | 放置火把并消耗库存 |
| `action.invalid.no_tool` | `Harvest` | 任意可采集方块 | 未持有匹配工具 | 动作失败，不推进进度 |

### 7.2 执行流程

| Step | 阶段 | 说明 |
| --- | --- | --- |
| 1 | 目标解析 | 获取目标格与目标材质 |
| 2 | 可达校验 | 必须满足 `reach.default_tiles` |
| 3 | Bundle 组装 | 根据手持物与目标类型选择 `bundle.harvest` 或 `bundle.place` |
| 4 | 能力校验 | 检查工具/物品是否满足对应 Action 规则 |
| 5 | 进度累积 | 按硬度、工具效率、基础耗时计算 |
| 6 | 结果提交 | 应用采集或放置结果，并触发掉落与反馈事件 |

### 7.3 挥舞与进度模型

| Key | 语义 | 约束 |
| --- | --- | --- |
| `action.swing_required` | Action 是否必须有挥舞表现 | `true`（镐、斧、剑） |
| `action.progress_decoupled_from_animation` | 进度是否与动画解耦 | `true` |
| `action.place.base_seconds` | 放置基础耗时 | `0.12` |
| `action.place.target_must_be_air` | 放置是否仅允许目标为空气 | `true` |

## 8. 右键 Interaction 优先级

| Priority | 条件 | 执行 |
| --- | --- | --- |
| 1 | 目标存在可交互对象 | 执行对象交互 |
| 2 | 手持物存在右键效果 | 执行手持物效果 |
| 3 | 其他情况 | 无动作 |

## 9. 背包与热栏

| Key | 语义 | 约束 |
| --- | --- | --- |
| `inventory.rows` | 背包总行数 | `>= 2` |
| `inventory.cols` | 每行格数 | `10` |
| `hotbar.active_row` | 当前热栏行索引 | `0 ... rows-1` |
| `hotbar.active_slot` | 当前热栏槽位 | `0 ... 9` |
| `hotbar.tab_cycle` | `Tab` 切行规则 | 循环切换 |

## 10. 可达与目标参数

| Key | 语义 | 范围/类型 |
| --- | --- | --- |
| `reach.default_tiles` | 默认可达半径 | `4` |
| `target.metric` | 距离计算方式 | `Euclidean2D` |
| `target.require_reachable` | 是否必须可达 | `true` |

## 11. 掉落与拾取事件

| Key | 语义 | 约束 |
| --- | --- | --- |
| `drop.spawn_on_harvest_commit` | 采集提交后是否生成世界掉落 | `true` |
| `drop.auto_insert_inventory` | 是否允许直接写背包 | `false` |
| `pickup.requires_contact` | 是否需要玩家接触拾取 | `true` |
| `pickup.side_effect.toast_text` | 拾取文本反馈 | `enabled` |
| `pickup.side_effect.sfx` | 拾取音效反馈 | `enabled` |
| `pickup.side_effect.quest_counter` | 任务/统计计数钩子 | `enabled` |

## 12. 光照与环境参数

| Key | 语义 | 范围/类型 |
| --- | --- | --- |
| `day_night.cycle_seconds` | 昼夜周期长度 | `float`（秒） |
| `light.sun_column_falloff` | 太阳光遮挡衰减 | `0.0 ~ 1.0` |
| `light.torch_radius_tiles` | 火把光照半径 | `6` |
| `light.intensity_cap` | 最终亮度上限 | `1.0` |

## 13. HUD 字段契约

| HUD Field | 类型 | 语义 |
| --- | --- | --- |
| `hud.hp_current` | `uint16` | 当前生命 |
| `hud.hp_max` | `uint16` | 最大生命 |
| `hud.resource_dirt` | `uint32` | 土资源数 |
| `hud.resource_stone` | `uint32` | 石资源数 |
| `hud.resource_wood` | `uint32` | 木资源数 |
| `hud.resource_coal` | `uint32` | 煤资源数 |
| `hud.item_torch` | `uint32` | 火把数量 |
| `hud.hotbar_row` | `uint8` | 当前热栏行 |
| `hud.hotbar_slot` | `uint8` | 当前热栏槽位 |
| `hud.smart_mode_enabled` | `bool` | 智能模式状态 |
| `hud.context_slot_visible` | `bool` | 上下文槽可见状态 |
