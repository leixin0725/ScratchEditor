# Quickstart: 剪贴板历史隔离验证

本流程只构建工作树内的开发产物，不部署稳定副本，不修改 AHK 或用户配置，也不读取或修改真实系统剪贴板。命令均从仓库根目录执行。

## Preconditions

- Windows 11，项目内 `.tools/Qt` 可用。
- 不运行 `release` preset，不省略 `-SkipLocalInstall`。
- 每个 runner 使用唯一 server name；新历史 runner 使用唯一临时 settings/history 目录并在 `finally` 中只终止自己启动的 PID。

## 1. Editing 构建与核心测试

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -Preset editing -SkipLocalInstall

.\build\editing\ScratchEditorClipboardHistoryTests.exe
```

核心测试必须覆盖：精确 Unicode/空白/换行去重、UTF-8 1MiB 边界、101 条淘汰、大小写不敏感搜索、UTF-16 字符数、稳定 ID、无 NUL/奇数字节/越界 Win32 缓冲、listener 注册失败、sequence 竞态、生产 Gate 拒绝、自身写入抑制、DPAPI roundtrip、密文不含明文、损坏/读写/加解密失败和 last-known-good 原子性。

## 2. 隔离进程与编辑回归

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-clipboard-history-tests.ps1 `
  -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.ClipboardHistory.Validation

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-editing-tests.ps1 `
  -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.Editing.ClipboardHistory
```

集成测试使用内存 clipboard gateway。验证窗口隐藏期间仍捕获、listener 失败和恢复、连续 10 次正常进程重启均恢复全文/ID/顺序、加载失败锁止、明确清空重置、Esc/Ctrl+S 产生一次记录、Ctrl+W 不写入。生产 Gate 拒绝由核心无窗口 fixture 验证，不启动普通实例。

## 3. Window UI 构建与验收

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -Preset window-ui -SkipLocalInstall

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-window-ui-tests.ps1 `
  -BuildSubdirectory build\window-ui `
  -ServerName ScratchEditor.WindowUi.ClipboardHistory `
  -ArtifactPrefix clipboard-history-window-ui
```

窗口验收检查：命令默认无快捷键；12px 触发条与 100/250ms nominal timer；命令打开后的搜索焦点；920px 宽窗口 push、500px 宽窗口 overlay；面板使用 `clamp(window.width/3, 200, 360)` 且编辑器至少 320px；动画关闭立即切换；摘要/时间/字数；搜索、稳定选择和重复置顶；单击不载入、双击/Enter 载入；dirty 确认/取消；删除和清空确认。external process 通过 test-only 状态文件验证无历史能力、store、命令和 panel Loader。

## 4. 性能验收

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-perf-tests.ps1 `
  -BuildSubdirectory build\window-ui `
  -ServerName ScratchEditor.Perf.ClipboardHistory `
  -ArtifactPrefix clipboard-history-perf
```

在 100 条多行记录下至少执行 20 次展开、搜索和重复置顶，等待下一 QML 帧后计算 p95，目标 `<= 100ms`；至少执行 20 次捕获到已打开面板可见的测量，p95 目标 `<= 500ms`。同时保留现有 idle CPU、working set 和 animation 门槛。

## 5. 环境完整性检查

完成后确认：

- runner 的精确临时 settings/history 目录已经清理；
- `%LOCALAPPDATA%\ScratchEditor\CodexEditor` 与 `AhkEditor` 未变化；
- `D:\_Dev\ScratchEditor\dev-links\AutoHotkey\KeysRedirect.ahk` 未变化；
- 没有访问或修改用户的生产 `clipboard-history.dat`；所有 test status 均为 `clipboardBackend=memory`、`nativeClipboardAccessAttempts=0`，且静态审计确认 Win32 clipboard API 只存在于 gateway；
- runner 启动的隔离实例已退出，其他 ScratchEditor 实例和受保护进程未被终止。

不得通过读取、保存或比较真实系统剪贴板内容来证明隔离。

## Deliberately Excluded

本 feature quickstart 不运行现有 `run-system-tests.ps1`，因为其中的遗留用例会直接读取、锁定、保存和恢复真实 Windows 剪贴板。它不是本功能的隔离验收依赖；只有用户明确批准真实系统回归后才运行完整的 release → system → external → editing → window-ui → AHK → perf 序列。
