# Third-Party 依赖统一策略

本项目采用统一的 `vendor-first` 策略：三方依赖优先从 `third_party/` 读取，默认不走在线拉取。

## 目录约定

- `third_party/SDL3-3.2.0/`
- `third_party/entt-3.13.2/src/entt/entt.hpp`
- `third_party/LuaJIT-2.1/src/lua.h`
- `third_party/LuaJIT-2.1/src/lua51.lib`（Windows）

## 安装方式（手动）

1. SDL3：下载官方开发包并解压到 `third_party/SDL3-3.2.0/`。
2. EnTT：下载 `v3.13.2` 源码并放到 `third_party/entt-3.13.2/`。
3. LuaJIT：下载 `v2.1` 源码到 `third_party/LuaJIT-2.1/`，在 `src/` 下执行 `msvcbuild.bat static` 生成 `lua51.lib`。

## CMake 行为

- 默认：仅依赖 `third_party/`（可复现、可离线、可审计）。
- 若必须走系统包管理器兜底，可显式开启：

```powershell
cmake -S . -B build -DNOVARIA_ALLOW_SYSTEM_DEPS=ON
```

## 注意

- `third_party` 下各依赖目录默认不纳入版本控制（见 `.gitignore`）。
- 如果从 git 仓库克隆依赖，请删除依赖目录内的 `.git`，避免嵌套仓库污染主仓库状态。
