# ScratchEditor

ScratchEditor 是从 AutoHotkey 临时编辑器迁移出的轻量 Windows 编辑器。主程序使用
Qt 6 Quick/QML、C++20 和 CMake；AutoHotkey 继续负责全局快捷键与启动调度，并通过
本地命名管道控制常驻的 `ScratchEditor.exe`。

迁移阶段 1–6 已完成。经用户明确批准并先创建同目录备份后，原
`D:\Documents\AutoHotkey\KeysRedirect.ahk` 已移除旧 GUI，只保留快捷键、Qt IPC
调度和启动失败时不改写内容的纯剪贴板回退。外部 AHK 文件和备份不属于本仓库。

## 功能

- 单实例常驻窗口，支持 `toggle`、`show`、`hide` 本地 IPC。
- 剪贴板载入/回写、Escape 关闭、焦点恢复和剪贴板异常保护。
- 无标题置顶窗口、四边/四角原生缩放、顶部与非缩放边框区域拖动、窗口几何记忆、自动换行和智能滚动条。
- CJK 字体、微软拼音、高 DPI 和暗色首帧保障。
- 原生 Markdown 语法高亮与常用 Markdown 编辑命令。
- 查找替换、延迟加载命令面板和可配置快捷键。
- 延迟加载设置页、深浅主题、编辑字体/字号，以及同步透明度与居中形变的轻量唤出/关闭动画开关。
- 窗口、外观和快捷键统一保存在一个带 schema 的 INI 配置文件中。
- 可作为 Codex 和 pi-coding-agent 的同步外部提示词编辑器，按文件启动独立瞬态进程。

按用户决定不提供 Markdown 预览。项目也不使用 Qt WebEngine、WebView 或其他浏览器
内核；历史草稿、标签页、固定草稿、多光标、插件和 LSP 当前均不在范围内。

## 编辑快捷键

以下是源码内置默认值；所有命令均可在命令面板中修改快捷键，用户配置可能与此不同。

| 命令 | 默认快捷键 |
|---|---:|
| 加粗 / 斜体 | `Ctrl+B` / `Ctrl+I` |
| 设为 1–6 级标题 | `Ctrl+Num+1`–`Ctrl+Num+6` |
| 标题向 6 级 / 1 级推进 | `Ctrl+Num+-` / `Ctrl+Num++` |
| 删除整行 | `Ctrl+Shift+L` |
| 切换任务项 / 切换本行 checkbox | `Ctrl+Alt+T` / `Ctrl+L` |
| 切换引用 / 切换代码标记 | `Ctrl+Shift+Q` / `Ctrl+Alt+C` |
| 查找 / 替换 | `Ctrl+F` / `Ctrl+H` |
| 命令面板 / 设置 | `Ctrl+Shift+P` / `Ctrl+,` |
| 循环标题级别 / 切换项目列表 | 无默认快捷键 |

- 加粗、斜体和代码命令在无选区时按 Qt 词边界处理相邻词语；在同类标记内部再次触发会取消对应格式，跨边界选区则只清理同类内部标记。
- 标题设置、推进和循环命令不会主动选中当前标题行；折叠光标会保持在正文中的相对位置。
- 空白行执行任一级标题命令会创建对应的 `# ` 到 `###### `；任何标题再次执行同级命令都会取消标题并保留正文。光标紧跟在行首标题前缀后时，一次 Backspace 也会删除完整前缀。
- 引用命令在空行生成 `> `，执行后不会保留自动选区。
- `Tab` 优先跳出括号、引号或 Markdown 强调标记；未触发跳出时，无论光标位于行内何处，都在行首增加 4 个空格。`Shift+Tab` 减少一级缩进。
- 括号、引号、行内代码与围栏代码支持自动补全，包含半角、全角及常用中文成对符号。
- 空 Markdown 标记对与空围栏代码可用一次退格整体删除；`……`、`——` 也支持整体退格删除，空行输入 `-` 会自动补为 `- `。
- Enter 自动接续无序列表、有序列表和任务复选框（新任务重置为未勾选），并维护后续同层有序列表编号；空列表项再次 Enter 会退出列表但保留空行，Backspace 则会整行删除并回到上一行末尾。Shift+Enter 和围栏代码块内仍为普通换行。
- `Ctrl+L` 切换光标所在行的 checkbox 勾选状态；非 checkbox 行会转换为未勾选任务，并保留已有列表编号、缩进和多层引用前缀。
- 光标已经位于文档最后一个可视行时，Down 会转到行尾；位于第一个可视行时，Up 会转到行首。

## Markdown 样式配置

Markdown 颜色和字体样式集中保存在 `config/markdown-style.json`。构建时该文件会复制到
程序目录下的 `config/markdown-style.json`，程序启动时读取；修改后重启 ScratchEditor
即可生效。行内代码和围栏代码的背景色、等宽字体以及链接下划线也由该文件管理。

## 架构与目录

```text
KeysRedirect.ahk ──命名管道──> ScratchEditor.exe
                                    ├─ C++：生命周期、IPC、剪贴板、配置、编辑命令、窗口过渡
                                    └─ QML：编辑器、查找、命令面板、设置页与界面动效
```

- `src/`：C++20 应用与编辑核心。
- `qml/`：预编译 Qt Quick 界面。
- `integration/`：隔离的 AHK 迁移参考副本。
- `tests/`：C++ 验收程序和 AHK 测试夹具。
- `scripts/`：构建、功能回归和性能验收入口。
- `docs/`：历史归档、验收报告与功能分支文档。
- `artifacts/baselines/`：纳入版本控制的阶段最终证据。

完整架构和阶段门槛见 [ScratchEditor-Migration.md](docs/archive/ScratchEditor-Migration.md)，文档索引见
[docs/README.md](docs/README.md)。

## 工具链与构建

已验证工具链：Qt 6.10.2、MinGW 13.1、CMake 3.25+、Ninja、Windows 11。
工作区工具链默认位于 `.tools/Qt`，该目录不会提交到 Git。

```powershell
./scripts/build.ps1
```

在不覆盖当前 `build/release` 实例的情况下验证当前源码，可使用：

```powershell
./scripts/build.ps1 -Preset stage4
```

也可以直接使用 CMake：

```powershell
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --preset release
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --build --preset release
```

`scripts/build.ps1` 会构建 `build/release` 并运行 `windeployqt`。如果该目录中的旧版本
正在运行，Windows 会锁定可执行文件。重建前应先发送 `--hide`，让实例回写剪贴板并
隐藏窗口，再通过任务管理器或受控进程管理停止该确切进程；`--hide` 本身不会退出进程，
构建脚本也不会强制终止用户实例。阶段 4 的已验证独立部署位于 `build/stage4/`。

## 运行与 IPC

```powershell
./build/release/ScratchEditor.exe --background
./build/release/ScratchEditor.exe --show
./build/release/ScratchEditor.exe --hide
./build/release/ScratchEditor.exe --toggle
```

以上四项是生产运行与单实例转发参数。不要使用 `ScratchEditor.exe --quit`：`quit` 只是在
`--test-mode` 隔离实例中使用的 JSON IPC 测试命令，不是命令行选项，也不向生产 IPC
开放。

常驻实例使用管道名 `ScratchEditor.Stage1.v1`，名称保持稳定是为了兼容既有 AHK 调度。
关闭窗口只会隐藏并复用进程，不会销毁编辑器。

正式配置存放在 Qt `AppConfigLocation` 下的 `settings.ini`。首次创建集中配置时会迁移
旧 Native Settings 中的窗口几何和快捷键；测试通过独立环境变量使用临时 INI。

## CLI 外部编辑器

文件位置参数会进入独立的外部编辑模式；`--wait` 是便于环境变量表达的兼容选项，进程本身
始终等待到编辑完成：

```powershell
./build/release/ScratchEditor.exe --wait ./prompt.md
```

文件模式读取和写回 UTF-8，绕过剪贴板、常驻单实例转发与生产 IPC。`Ctrl+S` 保存但继续
编辑；Escape 或关闭窗口会先保存，成功后以退出码 `0` 结束。保存失败时窗口保持打开。

为 Codex 持久配置 ScratchEditor（部署到 `%LOCALAPPDATA%\ScratchEditor\CodexEditor`，同时写入
Windows 用户环境变量，并修正 Git Bash 的 `~/.bashrc` 覆盖项）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1
```

脚本从 `build/release` 复制主程序并独立部署 Qt 运行库；配置完成后可以安全清理项目的 `build/`
和 `.tools/`，不会再破坏 Codex 的外部编辑器。首次安装或更新前若 `build/release` 不存在，先运行
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release`。
配置后需要重新打开 Git Bash 并重启 Codex，因为已经运行的进程不会重新读取环境变量。

### Codex 部署位置与更新

| 用途 | 固定位置 |
|---|---|
| 构建来源 | `D:\_Dev\ScratchEditor\build\release\ScratchEditor.exe` |
| Codex 稳定副本 | `%LOCALAPPDATA%\ScratchEditor\CodexEditor\ScratchEditor.exe` |
| `VISUAL` / `EDITOR` | `%LOCALAPPDATA%\ScratchEditor\CodexEditor\ScratchEditor.exe --wait` |

Codex 使用的是稳定副本，重新构建项目**不会自动更新**该副本。修改 ScratchEditor 后按顺序重新构建、
部署并检查：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1 -Action Install
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1 -Action Check
```

仅检查持久配置时可单独运行最后一条命令。`Install` 会覆盖更新稳定副本，但保持部署路径不变，
因此无需再次手动编辑环境变量或 `.bashrc`。

Codex 在 composer 中按 `Ctrl+G`；pi 也可在自己的 `settings.json` 中优先配置：

```json
{
  "externalEditor": "C:\\Users\\<用户名>\\AppData\\Local\\ScratchEditor\\CodexEditor\\ScratchEditor.exe --wait"
}
```

将 `<用户名>` 替换为实际 Windows 用户目录名；也可直接复制配置脚本输出的 `ExpectedCommand`。

当前已在原生 Windows 上实测 Codex CLI 0.146.0 与 pi 0.80.10 的完整 `Ctrl+G` 等待、写回
和返回流程。Claude Code 按本轮范围暂未实测。调查依据与后续边界见
[`docs/001-external-editor/`](docs/001-external-editor/)。

## 验收

外部文件核心、进程生命周期、并发会话与常驻实例隔离：

```powershell
./scripts/run-external-editor-tests.ps1
node ./scripts/run-external-cli-integration.mjs
```

第二条命令是本机 CLI 级联调，使用 ConPTY，并要求存在 `node-pty`；可通过
`SCRATCHEDITOR_NODE_PTY` 指向已安装的包目录。测试使用隔离的 Codex/pi 临时配置，写回
`/quit` 后退出，不发送模型请求，也不修改用户的 CLI 配置。

阶段 6 AHK 安装状态、备份、IPC、失败回退和进程保护验收：

```powershell
./scripts/run-stage6-tests.ps1
```

Qt 功能回归仍由阶段 4 入口覆盖阶段 3 和阶段 2。该历史入口的 AHK 基线参数应指向
阶段 6 备份：

```powershell
./scripts/run-stage4-tests.ps1 `
  -BuildSubdirectory build\stage4 `
  -OriginalAhkPath D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834
```

该入口还会检查四角缩放、边框拖动和编辑区域配色分层，并连续执行 20 轮唤出/关闭，
确认隐藏态几何稳定、窗口不会在关闭前回弹且再次唤出后恢复到记录尺寸。

完整性能回归：

```powershell
./scripts/run-stage1-tests.ps1 `
  -BuildSubdirectory build\stage4 `
  -ServerName ScratchEditor.Validation.Perf `
  -ArtifactPrefix validation-performance
```

所有当前测试使用独立管道和测试配置，不会停止默认管道上的用户实例。详细说明见
[tests/README.md](tests/README.md)。

阶段 6 最终实测：冷启动最大 75.18 ms、热唤醒 P95 19.54 ms、10 万字输入到帧
P95 16.61 ms、空闲 CPU 0%、工作集 39.61 MB、动画 59.88 FPS；微软拼音精确提交
`你好`。完整 JSON 证据保存在 [artifacts/baselines](artifacts/baselines/README.md)。

## AHK 迁移边界

`integration/KeysRedirect.QtMigration.ahk` 是阶段 2 的历史隔离参考副本，仍保留当时的
Qt/旧 GUI 回退开关，不会被构建或测试脚本自动安装。阶段 6 当前状态：

- 原文件备份：`D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834`。
- 备份 SHA-256：`8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9`。
- 已安装文件 SHA-256：`EF7CCD4E2CDDB0D29F8790F116A0B319CA1F2925548F48A05A5B195ADFE7D823`。
- 默认 Qt 路径仍为 `D:\_Dev\ScratchEditor\build\stage4\ScratchEditor.exe`，未迁移任何
  现有文件位置。
- 验收未重载正在运行的 AHK；磁盘上的新版本会在用户下次正常重载或登录时生效。

Qt 部署资源目前会输出已知的 `libpng iCCP` 警告，不影响功能、像素检查或性能验收。
