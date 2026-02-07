# 发布与诊断流程（Windows）

## 1. 打包流程

执行：

```powershell
.\tools\release\build_release.ps1 -BuildDir build -Config RelWithDebInfo -OutputRoot dist
```

说明：

- 当前发布流程以本地脚本执行为准，不依赖 CI 工作流。
- 发布前需手动执行构建、测试与打包命令并留存日志。

产物目录：

- `dist/novaria-<timestamp>/bin`：运行时可执行文件与 `SDL3.dll`
- `dist/novaria-<timestamp>/config`：默认配置
- `dist/novaria-<timestamp>/mods`：示例模组
- `dist/novaria-<timestamp>/symbols`：`pdb` 符号文件
- `dist/novaria-<timestamp>/checksums.sha256`：校验和清单

## 2. 崩溃转储与符号管理

发布要求：

1. `bin` 与 `symbols` 必须使用同一构建批次产出。
2. 发布包携带 `release-manifest.txt` 与 `checksums.sha256`。
3. 线上崩溃转储归档时，必须记录对应 `release-manifest.txt`。

建议：

- 符号目录按版本归档（例如 `symbols/2026-02-07/`）。
- 禁止覆盖历史符号，避免崩溃回溯错配。

## 3. 性能基准报告模板

每次候选发布必须附带一份基准报告，至少包含：

| 字段 | 说明 |
| --- | --- |
| `build_commit` | 发布候选 commit |
| `build_config` | `Release` 或 `RelWithDebInfo` |
| `scene_profile` | 测试场景描述（分辨率、实体密度、模组集） |
| `frame_time_p95_ms` | 帧时间 P95 |
| `frame_time_p99_ms` | 帧时间 P99 |
| `cpu_model` | CPU 型号 |
| `gpu_model` | GPU 型号 |
| `driver_version` | 显卡驱动版本 |
| `pass_fail` | 是否满足发布门槛 |

## 4. 发布门槛（当前）

- 双机 Soak（30 分钟）通过。
- 关键自动化测试 `ctest --test-dir build -C Debug --output-on-failure` 通过。
- 发布包校验和生成成功，且可执行文件可启动。
