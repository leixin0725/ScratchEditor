# 测试目录

## 自动验收程序

- `perf_main.cpp`：性能、真实 OS 输入和微软拼音验收客户端。
- `system_main.cpp`：滚动条、Escape、焦点和剪贴板异常回归。
  `run-system-tests.ps1` 仅为其启动的 test-mode 子进程设置
  `SCRATCHEDITOR_TEST_CLIPBOARD_BACKEND=native`，以验证真实 Win32 剪贴板锁定、写回与恢复；
  其他 test-mode 进程继续使用隔离的内存后端。
- `editing_main.cpp`：编辑行为验证，覆盖 Markdown 高亮、编辑命令、标题层级折叠/导航、
  折叠光标恢复、标题导航高亮范围、查找替换和快捷键回归。
- `clipboardhistory_main.cpp`：无窗口的协调器、内存 gateway、历史集合、二进制 codec、DPAPI、
  原子存储与故障边界测试；协调器用例覆盖异步加载、自写入回声抑制、错误优先级、
  `ReadLocked` 清空与关闭刷新。
- `window_ui_main.cpp`：窗口界面验证，覆盖设置页、主题字体、集中配置、标题折叠标记的常驻显示/配色/点击与滚动范围、gutter 折叠光标恢复、标题导航高亮层级/范围/时序、折叠/展开后下一帧正文像素即时消失/恢复、历史栏内外侧 hover/快速左向越界、窗口交互/配色、20 轮唤出关闭动画稳定性、闭合态窗口缩放时历史面板右边缘不外露回归、动画开启时缩放窗口编辑区即时跟随回归（历史面板开/合两种状态）、短文本下窗口缩放与轻量关闭动画滚动条不闪现回归、窗口放置算法单元校验（记忆恢复、混合 DPI 非主屏坐标、尺寸阶梯、锚定顺序与重叠避让）与明确排除项回归。
- `externalfilesession_main.cpp`：外部 CLI 编辑模式的 UTF-8/BOM、Unicode 路径、空文件、原子保存和错误边界回归。
- `externaleditorprocess_main.cpp`：验证外部编辑进程在编辑期间持续等待、保存写回后以成功状态退出、外部尺寸记忆独立写入 `window/externalGeometry`，并覆盖错误参数退出码。

需要 AHK 基线的 runner 默认使用仓库内 `integration/KeysRedirect.QtMigration.ahk`；如需通过
`-OriginalAhkPath` 指定项目外单个文件，必须先取得用户明确授权，禁止为此建立外部目录链接。
- `../scripts/run-external-cli-integration.mjs`：通过伪终端实测 Codex 与 pi 的 `Ctrl+G`，由 ScratchEditor 写回 `/quit` 后确认 CLI 成功返回；Claude Code 暂不在此脚本中测试。

CLI 级脚本需要一个已安装的 `node-pty` 包。本机默认复用全局 Gemini CLI 的依赖；其他环境可
通过 `SCRATCHEDITOR_NODE_PTY` 指定包目录。脚本为 Codex 和 pi 创建隔离临时配置，不发送模型
请求，也不读取或覆盖用户的认证和设置。

这些程序只连接由脚本启动的 `--test-mode` 隔离实例。生产 IPC 不暴露测试命令。

`../scripts/run-clipboard-history-tests.ps1` 会创建唯一的临时 settings/history 目录，运行无窗口核心
测试，并以显式虚拟变化验证隐藏捕获、排除规则、加密落盘、连续重启、损坏密文只读锁定和清理边界。
它只停止自己记录的 PID，且断言 backend 为 `memory`、原生剪贴板访问次数为 0。

`../scripts/run-editor-switch-tests.ps1` 使用唯一命名管道和临时 INI，在两个隔离构建产物之间验证
快捷键切换器的双向管道接管、只读状态查询、当前工作区/worktree 候选扫描、无效路径保护与启动失败
恢复，并验证成功时保留约 2 秒窗口缓冲、失败时显示手动关闭提示并进入错误保留状态。其余子用例
通过隔离环境覆盖值跳过重复等待，
worktree 用例只创建临时目录并通过隔离测试入口注入，不注册、修改或移除真实 worktree。
脚本只停止自己启动并通过 IPC 核验的 PID；停止前会等待隔离历史任务空闲并重新核验进程身份，
不进入生产保存流程，不连接默认生产管道，不读取正式配置或真实剪贴板。

## AHK 夹具

`fixtures/KeysRedirect.IpcTest.ahk` 只验证持久命名管道，不包含其他业务热键。它从
以下环境变量读取隔离目标：

- `SCRATCHEDITOR_SERVER_NAME`
- `SCRATCHEDITOR_EXE`

运行入口是 `../scripts/test-ahk-ipc.ps1`。夹具和迁移参考副本都不能覆盖用户原始
`KeysRedirect.ahk`。

AHK 迁移获批后，`../scripts/run-ahk-tests.ps1` 会从已安装文件创建临时测试副本，
验证以下边界：

- 同目录备份与迁移前原始哈希一致。
- 已安装文件与从备份执行的受控变换逐字节一致。
- 旧 AHK GUI 已删除，快捷键、启动预热和 IPC 保留。
- Qt 启动失败时剪贴板内容保持不变。
- 隔离 `show` / `hide` / `quit` 持久 IPC 正常，用户进程不被中断。

这里的 `quit` 是测试客户端通过命名管道发送的 JSON IPC 命令，只对 `--test-mode`
实例开放；它不是 `ScratchEditor.exe --quit` 命令行参数。测试清理应记录并停止隔离 PID，
或复用测试脚本中的 IPC 退出逻辑，不能向可执行文件传入 `--quit`。

## 推荐执行顺序

剪贴板历史日常开发应先执行以下完全隔离序列；它不会部署稳定副本，也不会读取、比较或修改真实
系统剪贴板：

1. `scripts/build.ps1 -Preset editing -SkipLocalInstall`
2. `build/editing/ScratchEditorClipboardHistoryTests.exe`
3. `scripts/run-clipboard-history-tests.ps1 -BuildSubdirectory build\editing -ServerName ScratchEditor.ClipboardHistory.Validation`
4. `scripts/run-editing-tests.ps1 -BuildSubdirectory build\editing -ServerName ScratchEditor.Editing.ClipboardHistory`
5. `scripts/build.ps1 -Preset window-ui -SkipLocalInstall`
6. `scripts/run-window-ui-tests.ps1 -BuildSubdirectory build\window-ui -ServerName ScratchEditor.WindowUi.ClipboardHistory`
7. `scripts/run-perf-tests.ps1 -BuildSubdirectory build\window-ui -ServerName ScratchEditor.Perf.ClipboardHistory`

下面的仓库完整回归包含真实系统集成。特别是 `run-system-tests.ps1` 的遗留用例会读取、保存、写入
并恢复真实 Windows 剪贴板；未经用户明确批准，不得把它作为剪贴板历史功能的自动验收步骤。

1. `scripts/build.ps1 -Preset release`
2. `scripts/run-system-tests.ps1`
3. `scripts/run-external-editor-tests.ps1`
4. `node scripts/run-external-cli-integration.mjs`
5. `scripts/build.ps1 -Preset editing -SkipLocalInstall`
6. `scripts/run-editing-tests.ps1`
7. `scripts/build.ps1 -Preset window-ui -SkipLocalInstall`
8. `scripts/run-ahk-tests.ps1`
9. `scripts/run-window-ui-tests.ps1`（AHK 基线参数指向迁移备份）
10. `scripts/run-perf-tests.ps1`（完整性能门槛）

最终结果应复制到 `artifacts/baselines/`；普通运行产生的时间戳结果默认被 Git 忽略。
