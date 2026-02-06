# ADR 索引

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 30e0b63
- `updated`: 2026-02-06

## 什么是 ADR

ADR（Architecture Decision Record）用于记录“为什么这么设计”，不是“怎么用系统”。

## 当前 ADR

- `docs/adr/ADR-0001-server-authoritative.md`：网络权威模型选择。
- `docs/adr/ADR-0002-script-runtime-boundary.md`：脚本运行时职责边界。

## 维护规范

- 新增 ADR 必须包含：背景、决策、备选方案、代价、回退条件。
- ADR 一旦接受不应覆盖改写；若决策变化，新增后续 ADR 说明替代关系。
