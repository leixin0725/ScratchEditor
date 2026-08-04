# 文档索引

当前使用方式和默认快捷键以根目录 [`README.md`](../README.md) 为准。文档按历史归档与
后续功能分支分开保存。

## 历史归档

各阶段报告是对应验收时点的历史快照，已记录的命令数量、快捷键、进程 ID、哈希和性能
数值不随后续功能改动重写。

- [`archive/ScratchEditor-Migration.md`](archive/ScratchEditor-Migration.md)：权威架构、约束和阶段路线。
- [`archive/STAGE1-PLAN.md`](archive/STAGE1-PLAN.md)：阶段 1 原型计划与验收方法。
- [`archive/STAGE1-REPORT.md`](archive/STAGE1-REPORT.md)：性能与输入原型报告。
- [`archive/STAGE2-REPORT.md`](archive/STAGE2-REPORT.md)：功能对等与 AHK 回退报告。
- [Markdown 编辑增强历史报告](archive/STAGE3-REPORT.md)（按用户决定不含预览）。
- [窗口界面历史报告](archive/STAGE4-REPORT.md)：设置页、外观、集中配置，以及窗口交互与动画稳定性回归。
- [`archive/STAGE5-REPORT.md`](archive/STAGE5-REPORT.md)：项目收尾、最终回归与 Git 初始化报告。
- [`archive/STAGE6-REPORT.md`](archive/STAGE6-REPORT.md)：原 AHK 备份、旧 GUI 移除、IPC/回退与最终验收报告。

各阶段最终机器可读证据统一保存在 [`../artifacts/baselines/`](../artifacts/baselines/)。
中间调优结果在本地 `artifacts/archive/` 中保留，但不进入版本控制。

## 功能分支

- [`001-external-editor/investigation-report.md`](001-external-editor/investigation-report.md)：Codex、pi-coding-agent 和 Claude Code 外部编辑器兼容性调查。
- [`001-external-editor/extension-plan.md`](001-external-editor/extension-plan.md)：外部编辑器瞬态文件模式的扩展计划与验收标准。
