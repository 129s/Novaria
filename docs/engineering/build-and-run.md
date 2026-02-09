# 构建与运行（Windows）

## 1. 前置条件

- Windows 10/11
- CMake 3.24+
- Visual Studio 2022（含 C++ 工具链）

## 2. 第三方依赖策略（统一）

- 统一采用 `vendor-only`：只从 `third_party/` 读取依赖，不在配置阶段在线拉取，不提供系统依赖兜底。
- 三方依赖统一目录约定：
  - `third_party/SDL3-3.2.0/`
  - `third_party/entt-3.13.2/`
  - `third_party/LuaJIT-2.1/`

## 3. 配置与编译

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## 4. 运行

```powershell
.\build\Debug\novaria.exe
```

说明：

- `config/default.cfg` 会在构建时被嵌入到可执行文件中，作为默认配置。
- 可执行文件同级的 `novaria.cfg`（或通过参数显式指定的 cfg）用于覆盖默认配置；覆盖文件解析失败时会被忽略（不会出现半覆盖状态）。

指定配置文件：

```powershell
.\build\Debug\novaria.exe novaria.cfg
```

独立服务端（无窗口）：

```powershell
.\build\Debug\novaria_server.exe --config novaria_server.cfg --ticks 7200
```

说明：

- `--ticks 0` 表示持续运行直到收到终止信号。
- `--fixed-delta` 可覆盖服务端 Tick 间隔（默认 `1/60`）。
- `--log-interval` 控制服务端诊断日志频率（默认每 `300` Tick）。

## 5. 常用可选参数

启用 LuaJIT vendor 探测（默认开启）：

```powershell
cmake -S . -B build -DNOVARIA_ENABLE_LUAJIT=ON
```

说明：若未找到 vendor LuaJIT，脚本运行时会在初始化阶段 fail-fast，不再回退到 stub。

运行时脚本后端配置（覆盖文件：`novaria.cfg`）：

```text
script_backend = "luajit"
```

模组脚本入口配置（`mods/<mod_name>/mod.cfg`）：

```text
script_entry = "content/scripts/main.lua"
script_api_version = "0.1.0"
script_capabilities = ["event.receive", "tick.receive"]
```

说明：

- `mods/core/mod.cfg` 为必需（core mod）；缺失会在启动装配阶段 fail-fast。
- `script_api_version` 与运行时 API 不一致会在初始化阶段 fail-fast。
- `script_capabilities` 声明超出运行时支持范围会在初始化阶段 fail-fast。

运行时网络后端配置（覆盖文件：`novaria.cfg`）：

```text
net_backend = "udp_peer"
net_udp_local_host = "127.0.0.1"
net_udp_local_port = 0
net_udp_remote_host = "127.0.0.1"
net_udp_remote_port = 0
```

说明：

- `net_udp_local_host` 控制本地绑定地址（`127.0.0.1` 仅同机，`0.0.0.0` 可接收外部主机数据包）。
- `net_udp_remote_port = 0` 时运行时允许通过首个 `SYN` 采纳动态 peer（同机默认仍可自环）。

同机双进程联调示例：

- 进程 A：`net_udp_local_host = "127.0.0.1"`，`net_udp_local_port = 25000`，`net_udp_remote_port = 25001`
- 进程 B：`net_udp_local_host = "127.0.0.1"`，`net_udp_local_port = 25001`，`net_udp_remote_port = 25000`

跨主机最小联调建议：

- 服务端：`net_udp_local_host = "0.0.0.0"`，`net_udp_local_port = 25000`，`net_udp_remote_host = "<客户端IP>"`，`net_udp_remote_port = 25001`
- 客户端：`net_udp_local_host = "0.0.0.0"`，`net_udp_local_port = 25001`，`net_udp_remote_host = "<服务端IP>"`，`net_udp_remote_port = 25000`

## 6. 输入与调试入口（单一事实源）

- 输入契约唯一事实源：`docs/architecture/input-and-debug-controls.md`。
- 本文档不再重复维护键位语义，避免与实现漂移。
- 调试输入开关：`debug_input_enabled`（默认 `false`，位于 `novaria.cfg`）。
- 审计口径：当 `debug_input_enabled=true` 且按下 `F1-F12` 时，运行时会输出 `input` 日志；默认关闭时调试键无效且不影响正式输入语义。

## 7. 验证建议

每次完成改动后执行：

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## 8. 跨主机 Smoke 工具

可执行文件：`build/Debug/novaria_net_smoke.exe`

Host 端示例：

```powershell
.\build\Debug\novaria_net_smoke.exe --role host --local-host 0.0.0.0 --local-port 25000 --remote-host 192.168.1.20 --remote-port 25001 --ticks 1200
```

Client 端示例：

```powershell
.\build\Debug\novaria_net_smoke.exe --role client --local-host 0.0.0.0 --local-port 25001 --remote-host 192.168.1.10 --remote-port 25000 --ticks 1200
```

判定标准：

- 两端都输出 `[PASS] novaria_net_smoke` 视为跨主机链路打通。
- 若失败，优先检查防火墙入站规则与 `remote_host/remote_port` 是否对齐。

## 9. 跨主机 Soak 工具（30 分钟稳定性）

可执行文件：`build/Debug/novaria_net_soak.exe`

Host 端示例（30 分钟）：

```powershell
.\build\Debug\novaria_net_soak.exe --role host --local-host 0.0.0.0 --local-port 25000 --remote-host 192.168.1.20 --remote-port 25001 --ticks 108000 --payload-interval 30
```

Client 端示例（30 分钟）：

```powershell
.\build\Debug\novaria_net_soak.exe --role client --local-host 0.0.0.0 --local-port 25001 --remote-host 192.168.1.10 --remote-port 25000 --ticks 108000 --payload-interval 30
```

判定标准：

- 两端都输出 `[PASS] novaria_net_soak`。
- 汇总信息中 `timeout_disconnects=0`。
- `received_payload_count > 0` 且会话至少进入过一次 `Connected`。

四节点并行 Soak（两组独立链路）：

```powershell
.\tools\net_soak_four_nodes.ps1 -BinaryPath .\build\Debug\novaria_net_soak.exe -Ticks 108000
```

故障注入 Soak（客户端暂停注入）：

```powershell
.\tools\net_soak_fault_injection.ps1 -BinaryPath .\build\Debug\novaria_net_soak.exe -Ticks 6000
```
