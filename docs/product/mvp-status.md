# Novaria MVP 状态看板

- `status`: active
- `owner`: @novaria-core
- `last_verified_commit`: 69a8ef7
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
- 备注：当前为 `NetServiceUdpLoopback` 验证，不是跨进程/跨主机实网端到端压力测试。

### DoD#4：第三方 Mod 扩展与一致性

- 状态：已覆盖（自动化）。
- 证据：
  - `tests/mvp/mvp_acceptance_tests.cpp` 的 `TestModContentConsistencyFingerprint`
  - `tests/mod/mod_loader_tests.cpp`

## 3. 验证命令

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
