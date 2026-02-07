# Simulation Pipeline

## 目标

定义 `sim/world/net/script/save/mod/ecs` 的统一调度顺序，保证行为可预测、可测试、可复盘。

## 固定调度顺序（`sim::SimulationKernel`）

1. 转发本地命令到 `net`。
2. 执行可识别世界命令（`world.set_tile`、`world.load_chunk`、`world.unload_chunk`）。
3. 执行可识别玩法命令（采集/建造/制作/战斗/Boss）。
4. `net.Tick`。
5. （仅连接态）消费远端快照并应用到 `world`。
6. `world.Tick`。
7. `ecs.Tick`（实体行为：投射物/目标碰撞/伤害/回收）。
8. `script.Tick`。
9. `world` 输出脏块并编码。
10. （仅连接态）`net.PublishWorldSnapshot`。

## 调度约束

- 顺序不可交换：网络输入必须先于世界推进，脚本必须晚于核心仿真。
- 脏块输出必须发生在 `world.Tick` 与 `ecs.Tick` 之后。
- 网络快照发布只允许在连接态执行。
