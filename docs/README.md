# Novaria 文档导航

- `status`: authoritative
- `owner`: @novaria-core
- `last_verified_commit`: 1937135
- `updated`: 2026-02-06

## 先看这里

- 5 分钟了解项目范围：`docs/product/mvp-definition.md`
- 5 分钟了解当前进度：`docs/product/mvp-status.md`
- 查里程碑执行状态：`docs/product/mvp-milestones.md`
- 查当前问题清单：`docs/product/mvp-issues.md`
- 查模块边界与契约：`docs/architecture/runtime-contracts.md`
- 查构建运行命令：`docs/engineering/build-and-run.md`
- 查测试策略与验收映射：`docs/engineering/testing.md`
- 查架构决策记录：`docs/adr/README.md`

## 文档分层（强约束）

- `product/`：目标、范围、DoD、状态看板、里程碑、问题清单。
- `architecture/`：模块契约、边界、数据流、约束。
- `engineering/`：构建、调试、测试、CI 工作流。
- `adr/`：关键架构决策与取舍。
- `archive/`：历史文档，仅保留追溯价值，不作为当前事实源。

## 维护规则

- 每份文档头部必须包含 `status/owner/last_verified_commit`。
- 同一事实只允许一个主声明文档，其他文档只放链接。
- `archive/` 文档禁止承载“当前状态”信息。
