# 文档索引

当前使用方式和默认快捷键以根目录 [`README.md`](../README.md) 为准。历史文档统一
保存在 `archive/`，命名说明见 [`archive/NAMING-NOTE.md`](archive/NAMING-NOTE.md)。

## 更新记录

- [`2026-08-08-editor-core-refactor/`](2026-08-08-editor-core-refactor/README.md)：编辑命令核心复用重构记录（行范围/成对包裹/命令分发/行变换拆分、IME 引号收尾统一与后续建议）。
- [`2026-08-06-status-panel/`](2026-08-06-status-panel/README.md)：右上角状态块拓展（字数统计、悬停状态面板、错误信息复制与状态面板配置）。
- [`2026-08-05-window-hotplug/`](2026-08-05-window-hotplug/README.md)：窗口放置与
  外部提示词编辑器更新（独立几何记忆、多屏热插拔修复、外部文件静默退出、CLI 类型标题）。
- `2026-08-06`：WSL 外部提示词编辑器部署适配，见根目录 [`README.md`](../README.md#cli-外部编辑器)
  的「CLI 外部编辑器」与 [`scripts/configure-codex-editor-wsl.sh`](../scripts/configure-codex-editor-wsl.sh)。
- `2026-08-06`：编辑器整行处理（三击选整行、无选区整行复制/剪切/智能粘贴），见根目录
  [`README.md`](../README.md#编辑快捷键) 的“编辑快捷键”。

## 历史归档

归档报告是对应验收时点的历史快照，已记录的命令数量、快捷键、进程 ID、哈希和性能
数值不随后续功能改动重写。归档内的 `stageN` 为 2026-08-04 重构前的旧命名，见
[`archive/NAMING-NOTE.md`](archive/NAMING-NOTE.md)。

- [`archive/ScratchEditor-Migration.md`](archive/ScratchEditor-Migration.md)：权威架构、约束和阶段路线。
- [`archive/STAGE1-PLAN.md`](archive/STAGE1-PLAN.md)：性能原型计划与验收方法。
- [`archive/STAGE1-REPORT.md`](archive/STAGE1-REPORT.md)：性能与输入原型报告。
- [`archive/STAGE2-REPORT.md`](archive/STAGE2-REPORT.md)：功能对等与 AHK 回退报告。
- [Markdown 编辑增强历史报告](archive/STAGE3-REPORT.md)（按用户决定不含预览）。
- [窗口界面历史报告](archive/STAGE4-REPORT.md)：设置页、外观、集中配置，以及窗口交互与动画稳定性回归。
- [`archive/STAGE5-REPORT.md`](archive/STAGE5-REPORT.md)：项目收尾、最终回归与 Git 初始化报告。
- [`archive/STAGE6-REPORT.md`](archive/STAGE6-REPORT.md)：原 AHK 备份、旧 GUI 移除、IPC/回退与最终验收报告。
- [`archive/cjk-features/`](archive/cjk-features/)：CJK 输入优化需求归档（已完结）——
  [最终验收清单](archive/cjk-features/cjk-features-final.md)、
  [修复计划](archive/cjk-features/cjk-features-fix-plan.md)、
  [原始实施计划](archive/cjk-features/cjk-features-plan.md)。
- [`archive/001-external-editor/investigation-report.md`](archive/001-external-editor/investigation-report.md)：Codex、pi-coding-agent 和 Claude Code 外部编辑器兼容性调查。
- [`archive/001-external-editor/extension-plan.md`](archive/001-external-editor/extension-plan.md)：外部编辑器瞬态文件模式的扩展计划与验收标准。

历史验收的机器可读证据统一保存在 [`../artifacts/baselines/`](../artifacts/baselines/)。
中间调优结果在本地 `artifacts/archive/` 中保留，但不进入版本控制。
