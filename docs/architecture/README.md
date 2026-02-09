# Architecture 文档导航

## 先看这里

- 调度主线与执行顺序：`docs/architecture/simulation-pipeline.md`
- 模块边界与契约：`docs/architecture/module-contracts.md`
- 输入与调试控制：`docs/architecture/input-and-debug-controls.md`
- Wire Protocol（二进制 v1）：`docs/architecture/wire-protocol.md`

## 维护规则

- `simulation-pipeline.md` 只描述统一调度顺序与跨模块时序约束。
- `module-contracts.md` 只描述模块职责边界与契约，不承载产品状态。
- `input-and-debug-controls.md` 只描述当前输入语义与调试入口。
- `wire-protocol.md` 只描述网络/存档的协议与编解码约束，不承载实现细节与进度。
