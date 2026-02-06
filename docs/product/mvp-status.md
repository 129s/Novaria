# Novaria MVP 状态看板

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 5d2d727
- `updated`: 2026-02-06

## 1. 总结论

- 当前是“工程化 MVP 验证态”，不是“完整产品交付态”。
- DoD 已建立自动化验收映射并落地测试。
- 14 周里程碑并非全部产品化完成，尤其 LuaJIT 正式接入仍未完成。

## 2. DoD 完成度

### DoD#1：可玩闭环 + 存档回读

- 状态：已覆盖（自动化）。
- 证据：`tests/mvp/mvp_acceptance_tests.cpp` 的 `TestPlayableLoopAndSaveReload`。

### DoD#2：性能 P95 ≤ 16.6ms

- 状态：已覆盖（自动化代理测试）。
- 证据：`tests/mvp/mvp_acceptance_tests.cpp` 的 `TestTickP95PerformanceBudget`。
- 备注：当前为本地测试机代理指标，正式发布前需补真实 1080p 场景基准报告。

### DoD#3：4 人 30 分钟联机稳定

- 状态：已覆盖（等效 Tick 仿真）。
- 证据：`tests/mvp/mvp_acceptance_tests.cpp` 的 `TestFourPlayerThirtyMinuteSimulationStability`。
- 备注：当前仍是 `NetServiceStub` 语义验证，不是实网端到端压力测试。

### DoD#4：第三方 Mod 扩展与一致性

- 状态：已覆盖（自动化）。
- 证据：
  - `tests/mvp/mvp_acceptance_tests.cpp` 的 `TestModContentConsistencyFingerprint`
  - `tests/mod/mod_loader_tests.cpp`

## 3. 里程碑状态（计划 vs 现实）

- W1-W2 工程骨架：完成（CMake/SDL3/日志/配置/测试基线）。
- W3-W6 单机闭环：完成最小纵切（玩法进度机 + Boss + 存档回读）。
- W7-W10 联机闭环：完成语义层闭环（会话、心跳、重连、诊断）；实网服未完成。
- W11-W12 Mod v0：完成清单依赖、内容定义加载、一致性指纹；LuaJIT VM 正式绑定未完成。
- W13-W14 性能发布：完成测试口径与代理验收；发布级基准与打包流程未完成。

## 4. 当前主要缺口

- LuaJIT 正式接入（当前是 `ScriptHostStub`）。
- 真正可部署的网络传输层（当前是 `NetServiceStub`）。
- 发布级 1080p 场景性能基准与构建产物流水线。

## 5. 验证命令

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
