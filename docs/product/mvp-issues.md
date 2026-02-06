# Novaria MVP Issues

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 5cba165
- `updated`: 2026-02-06

## 1. 问题清单

| 序号 | 完成状态 | 具体内容 |
| --- | --- | --- |
| 1 | 进行中 | LuaJIT 正式接入：已完成 `ScriptHostRuntime`、`LuaJitScriptHost` 与 `RuntimeDescriptor`（统一 `backend/api_version/sandbox` 元信息）；仍需完成生产级沙箱与内容脚本接入。 |
| 2 | 进行中 | 可部署网络传输层：已完成 `UdpTransport` 与 `NetServiceRuntime`（固定 `udp_loopback`，支持端口/对端配置与最小握手心跳）；仍需实现跨主机连接管理、重试策略与故障恢复策略。 |
