# AHK 集成边界

`KeysRedirect.QtMigration.ahk` 是迁移早期从用户脚本制作的历史隔离参考副本。它保留旧
AHK 临时编辑器，并通过 `UseQtScratchEditor` 开关选择 Qt IPC 或旧实现。

该文件仅用于审查和隔离验证：

- 不会被构建脚本安装到外部 AHK 仓库。
- 不会覆盖原 `KeysRedirect.ahk`。
- AHK 迁移已在用户明确批准和同目录备份后应用到原脚本；本参考副本不再代表安装状态。

精简的 IPC 测试夹具位于 `tests/fixtures/KeysRedirect.IpcTest.ahk`；AHK 迁移的受控变换
与安装验证入口分别是 `scripts/prepare-ahk-test.ps1` 和 `scripts/run-ahk-tests.ps1`。
