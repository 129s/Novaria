# 测试策略与矩阵

- `status`: authoritative
- `owner`: @novaria-core
- `last_verified_commit`: afd6494
- `updated`: 2026-02-06

## 1. 测试分层

- 单元层：模块级行为验证（`config/net/world/script/save/mod`）。
- 集成层：跨模块链路验证（`simulation_kernel`、`world_replication_flow`）。
- 验收层：MVP DoD 自动化代理验收（`novaria_mvp_acceptance_tests`）。

## 2. 运行命令

全量测试：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

仅跑 MVP 验收：

```powershell
ctest --test-dir build -C Debug --output-on-failure -R novaria_mvp_acceptance_tests
```

## 3. 测试目标清单

- `novaria_config_tests`
- `novaria_net_service_stub_tests`
- `novaria_udp_transport_tests`
- `novaria_world_service_tests`
- `novaria_script_host_stub_tests`
- `novaria_script_host_runtime_tests`
- `novaria_simulation_kernel_tests`
- `novaria_save_repository_tests`
- `novaria_mod_loader_tests`
- `novaria_world_snapshot_codec_tests`
- `novaria_world_replication_flow_tests`
- `novaria_mvp_acceptance_tests`

## 4. DoD 映射

### DoD#1 可玩闭环 + 存档回读

- 主证据：`TestPlayableLoopAndSaveReload`（`tests/mvp/mvp_acceptance_tests.cpp`）。
- 补充证据：`TestGameplayLoopCommandsReachBossDefeat`（`tests/sim/simulation_kernel_tests.cpp`）。

### DoD#2 性能 P95 ≤ 16.6ms

- 主证据：`TestTickP95PerformanceBudget`（`tests/mvp/mvp_acceptance_tests.cpp`）。

### DoD#3 4 人 30 分钟稳定协作

- 主证据：`TestFourPlayerThirtyMinuteSimulationStability`（`tests/mvp/mvp_acceptance_tests.cpp`）。
- 补充证据：重连/心跳诊断链路（`tests/sim/simulation_kernel_tests.cpp`）。

### DoD#4 Mod 扩展与一致性

- 主证据：`TestModContentConsistencyFingerprint`（`tests/mvp/mvp_acceptance_tests.cpp`）。
- 补充证据：`tests/mod/mod_loader_tests.cpp`（依赖排序、缺失/循环依赖、内容定义解析）。

## 5. 当前限制

- 联机稳定性验证当前基于 `NetServiceStub` 语义模型，不是实网端到端压力测试。
- 脚本层已接入 `ScriptHostRuntime` + `LuaJitScriptHost` 骨架，但尚未完成生产级 API 与安全策略。
