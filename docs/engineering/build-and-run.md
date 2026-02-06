# 构建与运行（Windows）

- `status`: authoritative
- `owner`: @novaria-core
- `last_verified_commit`: 2fc8b1d
- `updated`: 2026-02-06

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

禁用 LuaJIT 并固定走 stub：

```powershell
cmake -S . -B build -DNOVARIA_ENABLE_LUAJIT=OFF
```

运行时脚本后端配置（`config/game.toml`）：

```toml
script_backend = "auto"   # auto | stub | luajit
```

运行时网络后端配置（`config/game.toml`）：

```toml
net_backend = "stub"      # auto | stub | udp_loopback
net_udp_local_port = 0
net_udp_remote_host = "127.0.0.1"
net_udp_remote_port = 0
```

同机双进程联调示例：

- 进程 A：`net_backend = "udp_loopback"`，`net_udp_local_port = 25000`，`net_udp_remote_port = 25001`
- 进程 B：`net_backend = "udp_loopback"`，`net_udp_local_port = 25001`，`net_udp_remote_port = 25000`

## 6. 调试热键（当前）

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
