# 构建与运行（Windows）

- `status`: authoritative
- `owner`: @novaria-core
- `last_verified_commit`: 504e174
- `updated`: 2026-02-07

## 1. 前置条件

- Windows 10/11
- CMake 3.24+
- Visual Studio 2022（含 C++ 工具链）

## 2. SDL3 选择策略

- 默认优先使用本地目录：`third_party/SDL3-3.2.0/cmake`。
- 若系统已安装 SDL3，可手动指定 `SDL3_DIR`。
- 若允许联网，可启用 `NOVARIA_FETCH_SDL3=ON` 自动拉取。

## 3. 配置与编译

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## 4. 运行

```powershell
.\build\Debug\novaria.exe
```

指定配置文件：

```powershell
.\build\Debug\novaria.exe config/game.toml
```

独立服务端（无窗口）：

```powershell
.\build\Debug\novaria_server.exe --config config/game.toml --ticks 7200
```

说明：

- `--ticks 0` 表示持续运行直到收到终止信号。
- `--fixed-delta` 可覆盖服务端 Tick 间隔（默认 `1/60`）。
- `--log-interval` 控制服务端诊断日志频率（默认每 `300` Tick）。

## 5. 常用可选参数

使用系统 SDL3：

```powershell
cmake -S . -B build -DSDL3_DIR="你的SDL3Config.cmake所在目录"
```

允许联网自动拉取 SDL3：

```powershell
cmake -S . -B build -DNOVARIA_FETCH_SDL3=ON
```

启用 LuaJIT 自动探测（默认开启）：

```powershell
cmake -S . -B build -DNOVARIA_ENABLE_LUAJIT=ON
```

说明：若未找到 LuaJIT，脚本运行时会在初始化阶段 fail-fast，不再回退到 stub。

运行时脚本后端配置（`config/game.toml`）：

```toml
script_backend = "luajit"
```

模组脚本入口配置（`mods/<mod_name>/mod.toml`，可选）：

```toml
script_entry = "content/scripts/main.lua"
script_api_version = "0.1.0"
script_capabilities = ["event.receive", "tick.receive"]
```

说明：

- `script_api_version` 与运行时 API 不一致会在初始化阶段 fail-fast。
- `script_capabilities` 声明超出运行时支持范围会在初始化阶段 fail-fast。

运行时网络后端配置（`config/game.toml`）：

```toml
net_backend = "udp_loopback"
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

## 6. 玩家输入与调试热键（当前）

正式玩家输入（M6）：

- `W/A/S/D`：移动角色。
- `E`：挖掘角色面向方向的方块（可采集 `dirt/stone`）。
- `R`：放置当前选中材料到角色面向方向。
- `1/2`：切换放置材料（`dirt/stone`）。

兼容调试热键（保留）：

- `J/K`：提交基础动作命令。
- `F1`：脚本调试事件 `debug.ping`。
- `F2/F3`：挖空/放置方块。
- `F4/F5/F6`：断线/心跳/重连。
- `F7-F12`：玩法闭环调试（采集、建造、制作、战斗、Boss）。

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
