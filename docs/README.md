# 文档索引

各阶段报告是对应验收时点的历史快照，已记录的命令数量、快捷键、进程 ID、哈希和性能
数值不随后续功能改动重写。当前使用方式和默认快捷键以根目录
[`README.md`](../README.md) 为准。

- `../ScratchEditor-Migration.md`：权威架构、约束和阶段路线。
- `STAGE1-PLAN.md`：阶段 1 原型计划与验收方法。
- `STAGE1-REPORT.md`：性能与输入原型报告。
- `STAGE2-REPORT.md`：功能对等与 AHK 回退报告。
- `STAGE3-REPORT.md`：Markdown 编辑增强报告（按用户决定不含预览）。
- `STAGE4-REPORT.md`：设置页、外观、集中配置，以及后续窗口交互与动画稳定性回归报告。
- `STAGE5-REPORT.md`：项目收尾、最终回归与 Git 初始化报告。
- `STAGE6-REPORT.md`：原 AHK 备份、旧 GUI 移除、IPC/回退与最终验收报告。

各阶段最终机器可读证据统一保存在 `../artifacts/baselines/`。中间调优结果在本地
`artifacts/archive/` 中保留，但不进入版本控制。
