# Architecture 文档导航

## 先看这里

- 调度主线与执行顺序：`docs/architecture/simulation-pipeline.md`
- 模块边界与契约：`docs/architecture/module-contracts.md`
- 输入与调试控制：`docs/architecture/input-and-debug-controls.md`
- UI 系统与渲染边界：`docs/architecture/ui-system.md`
- 资源管线与打包约束：`docs/architecture/resource-pipeline.md`
- Wire Protocol（二进制 v1）：`docs/architecture/wire-protocol.md`

## 维护规则

- `simulation-pipeline.md` 只描述统一调度顺序与跨模块时序约束。
- `module-contracts.md` 只描述模块职责边界与契约，不承载产品状态。
- `input-and-debug-controls.md` 只描述当前输入语义与调试入口。
- `ui-system.md` 只描述 UI 数据流、渲染边界与组件化约束。
- `resource-pipeline.md` 只描述资源构建、版本化与运行期加载契约。
- `wire-protocol.md` 只描述网络/存档的协议与编解码约束，不承载实现细节与进度。
