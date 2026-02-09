# Mod 兼容性矩阵（M9）

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

## 4. 沙箱威胁模型（M1 收口）

- 目标：脚本仅能处理 `tick/event` 回调，不应具备文件系统、进程、网络、动态加载等宿主逃逸能力。
- 运行时策略：模块执行环境采用**白名单注入**，不从 `_G` 全量透传。
- 白名单（当前）：
  - 基础函数：`assert/error/ipairs/next/pairs/pcall/select/tonumber/tostring/type/xpcall`
  - 基础库：`math/string/table/coroutine`
  - 宿主上下文：`novaria`
- 禁用面（当前）：`io/os/debug/package/require/load/dofile/loadfile/jit/collectgarbage`。
- 回归口径：脚本运行时测试必须覆盖“禁用项不可见 + 白名单能力可用”。

## 5. 变更规则

1. 新增能力必须先写入本矩阵，再实现运行时支持。
2. 修改 `script_api_version` 必须同步更新迁移说明与测试样例。
3. 禁止“隐式兼容”：不在矩阵内的能力一律拒绝加载。
