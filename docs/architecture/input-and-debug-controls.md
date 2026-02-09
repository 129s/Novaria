# 输入与调试控制

## 目标

定义输入信号的分层、边界触发语义与调试入口约束，避免“调试键位污染正式体验”与“输入语义漂移导致回归不可控”。

## 输入分层（强约束）

### 1) `platform::InputActions`（平台原始动作）

- 产生方：`platform`（例如 SDL3 事件泵）。
- 语义：只表达“这一帧观察到的动作信号”，不包含玩法含义。
- 字段类型分两类：
  - **持续态（held）**：例如移动、鼠标左键按住。
  - **边界态（edge/pressed）**：例如按键按下的单次触发（必须做去抖，避免重复触发）。

### 2) `app::PlayerInputIntent`（玩家意图）

- 产生方：`app`（由 `InputCommandMapper` 从 `InputActions` 映射）。
- 语义：表达“玩家本帧想做什么”，仍不包含规则判断（例如“能不能挖/能不能做配方”）。
- 去冲突规则：
  - 左/右同时移动时必须消解为“静止”（避免输入状态不确定）。

## 正式输入语义（默认映射）

> 说明：这是面向玩家的默认语义约定。不同平台可有不同按键，但语义必须一致。

- 移动：
  - `move_left`：向左移动（默认 `A`）
  - `move_right`：向右移动（默认 `D`）
- 跳跃：
  - `jump_pressed`：单次触发跳跃（默认 `Space`）
- 主要动作：
  - `action_primary_held`：持续动作（默认鼠标左键按住）
  - `interaction_primary_pressed`：单次交互（默认鼠标右键按下）
- 热栏与 UI：
  - `hotbar_select_slot_1..10`：选择槽位
  - `hotbar_cycle_prev/next`：滚轮切换
  - `ui_inventory_toggle_pressed`：背包开关（默认 `Esc`）
  - `hotbar_select_next_row`：切换热栏行（默认 `Tab`）
- 智能模式：
  - `smart_mode_toggle_pressed`：切换智能模式（默认 `Ctrl`）
  - `smart_context_held`：智能上下文（默认 `Shift` 按住）

## 调试输入与入口（强约束）

> 原则：调试入口允许存在，但必须“可控、可隔离、可关闭”，且不得改变正式输入语义。

- **单一开关**：必须存在一个调试开关（配置或编译选项均可），默认关闭。
  - 当前实现：客户端 `novaria.cfg` 中的 `debug_input_enabled`（默认 `false`）。
- **零污染**：
  - 调试键位在开关关闭时必须完全无效。
  - 调试键位不得复用正式玩家语义字段（避免回归时难以定位来源）。
- **可审计**：调试输入触发必须有可观测输出（日志/计数），便于复盘。
  - 当前实现：`debug_input_enabled=true` 时，`F1-F12` 的触发会输出 `input` 诊断日志（no-op，不改变正式输入语义）。
