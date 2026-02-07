# Novaria MVP Issues

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: bcee1f4
- `updated`: 2026-02-06

## 1. 问题清单

| 序号 | 完成状态 | 具体内容 |
| --- | --- | --- |
| 1 | 进行中 | LuaJIT 正式接入：已完成 `ScriptHostRuntime`、`LuaJitScriptHost`、`RuntimeDescriptor` 与模组脚本装载链路（`script_entry/script_api_version` + API 版本 fail-fast + MVP 最小沙箱 + 指令预算保护）；仍需完善生产级沙箱策略（资源配额与隔离）。 |
| 2 | 进行中 | 可部署网络传输层：已完成 `UdpTransport` 与 `NetServiceRuntime`（固定 `udp_loopback`，支持本地绑定地址配置、端口/对端配置、最小握手心跳、连接探测指数退避、对端来源校验与动态 peer 采纳）；仍需补跨主机实网压力验证与更完整故障恢复策略。 |
