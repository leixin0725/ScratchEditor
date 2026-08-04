# 外部编辑器兼容性调查报告

调查日期：2026-08-03
调查范围：仅研究 Codex、pi-coding-agent 和 Claude Code CLI 通过 `Ctrl+G` 等入口唤起
ScratchEditor 编辑提示词的传统外部编辑器流程；不包含 MCP、插件、智能体工具或项目文件操作。

## 结论

ScratchEditor 可以作为三种 CLI 共用的外部编辑器。调查时的基线版本不能直接使用；当前
`001-external-editor` 分支已经补齐 Codex 与 pi 所需的同步临时文件流程：CLI 写入临时文件，
把文件路径追加到编辑器命令，等待编辑器退出，再读取文件内容。

结论仍然是：无需 Windows 注册表、插件、MCP 或智能体工具注册。除读写指定文件外，关键
能力是“编辑完成”的同步语义；否则 CLI 会过早读回，或者永久等待。

## 本分支实现与验证

当前增量实现提供 `ScratchEditor.exe [--wait] <file>`：

- 每个文件请求启动独立瞬态进程，不转发给常驻剪贴板实例，也不占用其 IPC 管道。
- 严格读取 UTF-8，可识别并移除输入 BOM；使用 `QSaveFile` 原子写回无 BOM UTF-8。
- `Ctrl+S` 只保存；Escape 或关闭窗口保存成功后退出 `0`，保存失败则保留窗口和文本。
- 空文本、Unicode/CJK/emoji、含空格和中文的路径、无效 UTF-8、缺失路径、并发双会话，
  以及常驻实例共存均由自动化测试覆盖。
- Codex CLI 0.146.0 与 pi 0.80.10 已在原生 Windows ConPTY 中完成真实 `Ctrl+G` 验收：
  ScratchEditor 写回 `/quit` 后，两种 CLI 均成功读取并退出。测试使用隔离配置且未发送模型
  请求。
- Claude Code 按本轮用户决定暂不测试；实现仍遵循相同的文件与等待契约。

## CLI 调用约定

| CLI | 编辑器选择方式 | CLI 恢复条件 |
|---|---|---|
| Codex | `VISUAL`，未设置时回退到 `EDITOR` | 用户保存并关闭编辑器 |
| pi-coding-agent | `externalEditor`，然后依次回退到 `VISUAL`、`EDITOR`；Windows 最终回退到 Notepad | 编辑器子进程以退出码 `0` 结束 |
| Claude Code | `Ctrl+G` 或 `Ctrl+X Ctrl+E` 打开系统配置的文本编辑器 | 用户保存并关闭编辑器 |

Codex 官方文档明确说明，`Ctrl+G` 使用 `VISUAL`/`EDITOR`，保存并关闭后把文本带回 composer。
pi 的官方设置文档以 `code --wait` 为例，说明调用方需要等待编辑器进程。Claude Code 官方
交互模式文档把 `Ctrl+G` 定义为“在默认文本编辑器中打开”。为获得一致行为，部署时应让
`VISUAL` 和 `EDITOR` 指向同一个 ScratchEditor 等待命令；pi 也可使用自己的
`externalEditor` 设置覆盖环境变量。

仓库提供 [`scripts/configure-codex-editor.ps1`](../../scripts/configure-codex-editor.ps1)，将程序及 Qt
运行库部署到 `%LOCALAPPDATA%\ScratchEditor\CodexEditor`，持久写入 Windows 用户级
`VISUAL`/`EDITOR`，并在 Git Bash 的 `~/.bashrc` 末尾维护同一命令。pi 的 `externalEditor` 也指向这个
共享副本。后者很重要：shell 启动文件中的 `export VISUAL=...` 会覆盖 Windows 用户环境变量。脚本的
`-Action Check` 会检查两个稳定部署、用户环境变量、pi、AHK 和 Git Bash 配置；项目的 `build/` 或
`.tools/` 被清理后，外部编辑器仍可继续工作。脚本还会生成被 Git 忽略的
`docs/codex-editor-installation.local.md`，集中记录本机实际路径和更新命令。标准
`scripts/build.ps1` 在每次成功构建后自动用刚构建的程序同步 Codex/pi 与 AHK 两个稳定副本；仅在显式
指定 `-SkipLocalInstall` 或直接运行 CMake 时跳过。
受管 Git Bash 配置还会检查 `TERM_PROGRAM=vscode`：VS Code 集成终端使用 `code --wait`，普通终端
使用稳定部署的 ScratchEditor。该选择只影响 `Ctrl+G`；可点击文件引用仍由 Codex 的 `file_opener`
独立控制。

## 实施前基线的阻塞点

### 命令行没有文件入口

[`src/main.cpp`](../../src/main.cpp) 只注册 `background`、`toggle`、`show`、`hide` 和
`test-mode`，没有声明或读取位置参数。CLI 即使把临时文件路径传给程序，当前代码也不会将其
作为编辑内容加载。

### 编辑内容绑定到剪贴板

[`src/editorcontroller.cpp`](../../src/editorcontroller.cpp) 的 `showEditor()` 从剪贴板加载
文本，`commitAndHide()` 在隐藏前写回剪贴板。该行为适合现有全局快捷键草稿窗，但与外部
编辑器所要求的“读取并覆盖调用方指定文件”是两种不同的数据源。

此外，当前 `commitAndHide()` 只在文本非空时写剪贴板。外部文件模式必须允许写入空文件，
否则用户无法通过清空编辑器来清空 CLI composer。

### 单实例生命周期与同步等待冲突

当常驻实例已经存在时，[`src/main.cpp`](../../src/main.cpp) 会把命令转发给现有实例并立即
退出。CLI 会因此立即读取临时文件，而用户尚未完成编辑。没有常驻实例时，程序又设置了
`quitOnLastWindowClosed(false)`，关闭窗口只会隐藏常驻进程，CLI 将一直等待。

生产 IPC 在 [`EditorController::dispatchCommand()`](../../src/editorcontroller.cpp) 中只接受
`toggle`、`show` 和 `hide`；测试命令不会在生产模式开放。这意味着当前 IPC 也没有文件会话
标识、完成通知或等待者连接。

### 窗口关闭始终被改写为隐藏

[`qml/Main.qml`](../../qml/Main.qml) 的 `onClosing` 总是拒绝原生关闭事件并调用
`hideEditor()`，Escape 也执行同一操作。外部文件模式需要把这些入口解释为“保存并结束本次
文件编辑进程”，而不是继续隐藏常驻窗口。

## 推荐兼容边界

最小且可靠的方案是增加独立的瞬态文件模式，同时保持现有剪贴板常驻模式不变：

```text
无文件参数
  -> 当前单实例、剪贴板、show/hide/toggle 行为

[--wait] <file>
  -> 独立进程、读取指定文件、保存后退出、退出码报告结果
```

文件模式不加入当前固定名称的 `QLocalServer`，也不向常驻实例转发。每个外部编辑请求使用
自己的进程和文件路径，因此 Codex、pi 和 Claude Code 同时发起请求时不会争用一个窗口或
覆盖彼此的内容。这比在常驻实例内增加请求 ID、会话队列、完成通知和代理等待进程更简单，
故障边界也更清晰。

## 必需能力

1. 接受一个包含空格、Unicode 和长路径的位置参数。
2. 以 UTF-8 读取 CLI 创建的临时文件，并在编辑区展示其完整内容。
3. 支持显式保存，并在窗口关闭或 Escape 时完成最终保存。
4. 保存成功后退出进程并返回 `0`；命令行、读取或保存失败时返回非零值。
5. 文件保存失败时保持窗口和未保存内容，不得伪装成成功退出。
6. 文件模式绕过剪贴板读取、剪贴板写回、常驻单实例转发和隐藏动画终态。
7. 空文本必须覆盖为零字节或约定的 UTF-8 空文件。

## 不需要增加的能力

此次兼容不要求文件浏览器、目录树、多标签页、LSP、代码补全、插件系统、MCP、标准输入
协议、模型 API 或智能体可调用工具。Windows 文件关联也不是三种 CLI 共同依赖的注册机制。

## 平台注意事项

原生 Windows CLI 可以直接把 Windows 临时文件路径传给 `ScratchEditor.exe`。如果 CLI
运行在 WSL，而 ScratchEditor 仍是 Windows GUI，则需要一个很薄的启动脚本用 `wslpath -w`
转换临时文件路径，并把 Windows 编辑器的退出状态传回 WSL。该包装属于部署适配，不应污染
核心文件会话模型。

## 资料来源

- [Codex CLI customization：Prompt editor](https://learn.chatgpt.com/docs/cli-customization#prompt-editor)
- [pi-coding-agent settings：externalEditor](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/settings.md#ui--display)
- [pi-coding-agent keybindings](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/keybindings.md#application)
- [Claude Code interactive mode：General controls](https://code.claude.com/docs/en/interactive-mode#general-controls)
- [Claude Code keybindings：chat:externalEditor](https://code.claude.com/docs/en/keybindings#chat-actions)
