# 编辑核心重构与围栏语法待决事项

已按优先级完成控制器测试设施隔离、输入滚动与拖动状态提取、列表纯文本变换提取及 editing 验收拆分。职责边界见[运行与 IPC](../integration/README.md)、[编辑核心](../editor/README.md)和[测试指南](../../tests/README.md)。

最终隔离验收通过 873 个 editing 行为检查和 130 个 window-ui 行为检查，editing 检查名、详情键与重构前一致。剪贴板历史集成通过。完整 perf 的系统拼音在正文精确提交“你好”；冷启动最慢 390.5 ms、空闲工作集 94.5 MiB，原门槛分别为 300 ms、80 MiB。两项重构前已超限，本次按确认的基线例外验收。报告保留原始判定，详见[最终验收基线](../../artifacts/baselines/editor-core-refactor-20261112.zip)。

## 待确认的围栏语法

CJK 编辑保护、标题折叠和语法高亮使用的围栏识别规则仍有差异。统一前需要确认波浪号能否开启围栏、关闭行是否必须匹配开启字符与最小长度，以及尾部是否只允许空白。确认后按独立行为修复补回归。调查时的代码证据见[原始调查第 3 节](../archive/2026-11-12-editor-core-refactor/editor-core-refactor.md#3-已存在规则分歧markdown-围栏识别)。
