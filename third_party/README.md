# Third-Party 依赖（vendor-only）

本项目依赖统一采用 **vendor-only**：只允许从仓库根目录的 `third_party/` 读取，不提供系统包管理器兜底。

## 目录约定

- `third_party/SDL3-3.2.0/`
- `third_party/entt-3.13.2/src/entt/entt.hpp`
- `third_party/LuaJIT-2.1/src/lua.h`
- `third_party/LuaJIT-2.1/src/lua51.lib`（Windows）

## 获取方式（建议）

1. SDL3：按上面目录放置完整 vendor 包。
2. EnTT：使用 `v3.13.2` 并放置在 `third_party/entt-3.13.2/`。
3. LuaJIT：使用 `v2.1` 并放置在 `third_party/LuaJIT-2.1/`；Windows 需在 `src/` 下构建生成 `lua51.lib`。

## 备注

- `third_party/*` 通常不入库（见仓库根目录 `.gitignore`），仅保留本 README 作为目录约定。
