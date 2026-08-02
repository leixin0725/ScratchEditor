# 测试目录

## 自动验收程序

- `perf_main.cpp`：阶段 1 性能、真实 OS 输入和微软拼音验收客户端。
- `stage2_main.cpp`：滚动条、Escape、焦点和剪贴板异常回归。
- `stage3_main.cpp`：Markdown 高亮、编辑命令、查找替换和快捷键回归。
- `stage4_main.cpp`：设置页、主题字体、集中配置、窗口交互/配色、20 轮唤出关闭动画稳定性与明确排除项回归。

这些程序只连接由脚本启动的 `--test-mode` 隔离实例。生产 IPC 不暴露测试命令。

## AHK 夹具

`fixtures/KeysRedirect.Stage1Test.ahk` 只验证持久命名管道，不包含其他业务热键。它从
以下环境变量读取隔离目标：

- `SCRATCHEDITOR_SERVER_NAME`
- `SCRATCHEDITOR_EXE`

运行入口是 `../scripts/test-ahk-ipc.ps1`。夹具和迁移参考副本都不能覆盖用户原始
`KeysRedirect.ahk`。

阶段 6 获批后，`../scripts/run-stage6-tests.ps1` 会从已安装文件创建临时测试副本，
验证以下边界：

- 同目录备份与阶段 5 原始哈希一致。
- 已安装文件与从备份执行的受控变换逐字节一致。
- 旧 AHK GUI 已删除，快捷键、启动预热和 IPC 保留。
- Qt 启动失败时剪贴板内容保持不变。
- 隔离 `show` / `hide` / `quit` 持久 IPC 正常，用户进程不被中断。

这里的 `quit` 是测试客户端通过命名管道发送的 JSON IPC 命令，只对 `--test-mode`
实例开放；它不是 `ScratchEditor.exe --quit` 命令行参数。测试清理应记录并停止隔离 PID，
或复用测试脚本中的 IPC 退出逻辑，不能向可执行文件传入 `--quit`。

## 推荐执行顺序

1. `scripts/build.ps1 -Preset stage4`
2. `scripts/run-stage6-tests.ps1`
3. `scripts/run-stage4-tests.ps1`（AHK 基线参数指向阶段 6 备份）
4. `scripts/run-stage1-tests.ps1`（完整性能门槛）

最终结果应复制到 `artifacts/baselines/`；普通运行产生的时间戳结果默认被 Git 忽略。
