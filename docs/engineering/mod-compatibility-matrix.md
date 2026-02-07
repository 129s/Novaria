# Mod 兼容性矩阵（M9）

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 504e174
- `updated`: 2026-02-07

## 1. 目标

为 `script_api_version + script_capabilities` 提供统一兼容口径，避免模组在运行时“静默降级”。

## 2. 运行时支持矩阵（当前）

| 维度 | 支持值 | 行为 |
| --- | --- | --- |
| `script_api_version` | `0.1.0` | 允许加载 |
| `script_api_version` | 其他值 | 初始化 fail-fast |
| `script_capabilities` | `event.receive` | 允许加载 |
| `script_capabilities` | `tick.receive` | 允许加载 |
| `script_capabilities` | 其他能力 | 初始化 fail-fast |

## 3. 兼容性测试清单

- API 版本匹配：应通过。
- API 版本不匹配：应 fail-fast。
- 能力声明白名单内：应通过。
- 能力声明白名单外：应 fail-fast。
- 能力声明为空：运行时应按默认能力集补全。

## 4. 变更规则

1. 新增能力必须先写入本矩阵，再实现运行时支持。
2. 修改 `script_api_version` 必须同步更新迁移说明与测试样例。
3. 禁止“隐式兼容”：不在矩阵内的能力一律拒绝加载。
