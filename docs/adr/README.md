# ADR 索引

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 30e0b63
- `updated`: 2026-02-06

## 什么是 ADR

ADR（Architecture Decision Record）用于记录“为什么这么设计”，不是“怎么用系统”。

## 索引表

| 序号 | 主题 | 文件 | 状态 | 说明 |
| --- | --- | --- | --- | --- |
| 1 | Server-Authoritative 网络模型 | `docs/adr/server-authoritative.md` | accepted | 定义联机权威边界与一致性策略 |
| 2 | 脚本运行时职责边界 | `docs/adr/script-runtime-boundary.md` | accepted | 约束 LuaJIT/脚本职责，避免侵入高频核心循环 |

## 维护规范

- 新增 ADR 必须包含：背景、决策、备选方案、代价、回退条件。
- ADR 一旦接受不应覆盖改写；若决策变化，新增后续 ADR 说明替代关系。
