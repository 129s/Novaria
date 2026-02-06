# Novaria MVP Issues

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 0b1f5c8
- `updated`: 2026-02-06

## 1. 问题清单

| 序号 | 完成状态 | 具体内容 |
| --- | --- | --- |
| 1 | 进行中 | LuaJIT 正式接入：已完成 `ScriptHostRuntime` 与 `LuaJitScriptHost` 生命周期骨架（含自动回退 stub）；仍需完成脚本 API 版本化、生产级沙箱与内容脚本接入。 |
| 2 | 进行中 | 可部署网络传输层：已完成 `UdpTransport`（端口绑定、非阻塞收发、Loopback 自测）；仍需接入 `INetService` 生产路径并补连接管理与故障恢复。 |
