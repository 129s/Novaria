# Novaria MVP Issues

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 64cca05
- `updated`: 2026-02-06

## 1. 问题清单

| 序号 | 完成状态 | 具体内容 |
| --- | --- | --- |
| 1 | 进行中 | LuaJIT 正式接入：已完成 `ScriptHostRuntime` 与 `LuaJitScriptHost` 生命周期骨架（含自动回退 stub）；仍需完成脚本 API 版本化、生产级沙箱与内容脚本接入。 |
| 2 | 进行中 | 可部署网络传输层：已完成 `UdpTransport` 与 `NetServiceRuntime`（`stub/udp_loopback` 可切换）；仍需实现跨进程/跨主机连接管理、重试策略与故障恢复策略。 |
