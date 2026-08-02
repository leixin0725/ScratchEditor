# AHK 集成边界

`KeysRedirect.QtMigration.ahk` 是从用户脚本制作的隔离迁移参考副本。它保留旧 AHK
临时编辑器，并通过 `UseQtScratchEditor` 开关选择 Qt IPC 或旧实现。

该文件仅用于审查和隔离验证：

- 不会被构建脚本安装到外部 AHK 仓库。
- 不会覆盖原 `KeysRedirect.ahk`。
- 阶段 6 未经明确批准不得把删除旧 GUI 的变更应用到原脚本。

精简的 IPC 测试夹具位于 `tests/fixtures/KeysRedirect.Stage1Test.ahk`。
