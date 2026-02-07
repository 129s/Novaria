# Novaria MVP Issues

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 504e174
- `updated`: 2026-02-07

## 1. 问题清单

| 序号 | 优先级 | 完成状态 | 关联里程碑 | 具体内容 |
| --- | --- | --- | --- | --- |
| 1 | P0 | 已完成 | M8 | 联机实网验证链路已补齐：`novaria_net_soak`（30 分钟）、四节点并行脚本与故障注入脚本均可执行。 |
| 2 | P0 | 已完成 | M9 | LuaJIT 沙箱已接入指令预算、内存配额、模块隔离环境与能力白名单校验。 |
| 3 | P0 | 已完成 | M6 | 可见渲染纵切已落地：Tile/实体/相机与基础 HUD 可稳定运行。 |
| 4 | P0 | 已完成 | M6-M7 | 玩家交互已从调试热键收敛到正式链路（`W/A/S/D + E/R + 1/2`）。 |
| 5 | P1 | 已完成 | M8 | 独立 `server` 可执行已补齐（`novaria_server`），可用于联机压测与故障定位。 |
| 6 | P1 | 已完成 | M7-M10 | 存档已接入世界 Chunk 快照持久化与 `world.sav.bak` 回档副本，版本字段校验可用。 |
| 7 | P1 | 已完成 | M9 | Mod API 已接入 `script_capabilities` 声明、运行时校验与兼容矩阵文档。 |
| 8 | P2 | 已完成 | M10 | 发布链路已补齐：打包脚本、符号归档、基准模板与 CI 自动构建打包流程。 |
| 9 | P0 | 已完成 | M11 | EnTT 迁移边界定稿：`world(tile/chunk)` 保持权威数据层，不迁入 ECS；仅 `projectile/hostile_target` 行为实体进入 ECS。 |
| 10 | P0 | 已完成 | M11 | `GameApp` 解耦：已拆分输入映射、玩家控制、渲染组装，入口层状态已收敛到控制器对象。 |
| 11 | P0 | 已完成 | M11-M12 | EnTT 运行时骨架已落地：`registry/context/system pipeline` 与固定 Tick 调度顺序对齐。 |
| 12 | P1 | 已完成 | M12 | 投射物纵切已落地：生成→移动→碰撞→伤害→回收端到端可测。 |
| 13 | P1 | 已完成 | M12 | 指令桥接已强类型化：`TypedPlayerCommand` 统一解码，替换核心字符串分发路径。 |
| 14 | P1 | 已完成 | M12-M13 | 验证与回归补齐：新增 `novaria_ecs_runtime_tests`，全量 `ctest` 通过。 |

## 2. 近两周执行焦点（W15-W16）

1. 已完成 M6：Tile/实体可见化与玩家交互链路已落地。
2. 已完成联机验证基线：Soak、四节点并行、故障注入脚本可执行。
3. 已完成发布工程化基线：打包脚本、符号归档与 CI 打包流程接通。
