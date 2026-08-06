# ScratchEditor

ScratchEditor 是从 AutoHotkey 临时编辑器迁移出的轻量 Windows 编辑器。主程序使用
Qt 6 Quick/QML、C++20 和 CMake；AutoHotkey 继续负责全局快捷键与启动调度，并通过
本地命名管道控制常驻的 `ScratchEditor.exe`。

迁移阶段 1–6 已完成。经用户明确批准并先创建同目录备份后，原
`D:\Documents\AutoHotkey\KeysRedirect.ahk` 已移除旧 GUI，只保留快捷键、Qt IPC
调度和启动失败时不改写内容的纯剪贴板回退。外部 AHK 文件和备份不属于本仓库。

## 功能

- 单实例常驻窗口，支持 `toggle`、`show`、`hide` 本地 IPC。
- 剪贴板载入/回写、Escape 关闭并复制、Ctrl+S 关闭并把内容输入到下一个窗口、Ctrl+W 关闭且不保存（完全回退到打开前状态）、关闭后焦点交给最近活跃窗口（外部 CLI 模式除外）和剪贴板异常保护。
- 无标题置顶窗口、四边/四角原生缩放、顶部与非缩放边框区域拖动、窗口几何记忆（临时编辑器与外部提示词编辑器独立）、自动换行和智能滚动条。
- CJK 字体、微软拼音、高 DPI 和暗色首帧保障。
- 原生 Markdown 语法高亮与常用 Markdown 编辑命令。
- 查找替换、延迟加载命令面板和可配置快捷键。
- 可直接拖动已有文本选区移动内容，支持跨行落点、边缘自动滚动和单步撤销。
- 延迟加载设置页、深浅主题、编辑字体/字号，以及同步透明度与居中形变的轻量唤出/关闭动画开关。
- 窗口、外观和快捷键统一保存在一个带 schema 的 INI 配置文件中。
- 可作为 Codex 和 pi-coding-agent 的同步外部提示词编辑器（标题标注调用它的 CLI 类型），按文件启动独立瞬态进程。

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
- 空白行执行任一设置/循环标题命令会创建对应的 `# ` 到 `###### `；`Ctrl+Num+-` / `Ctrl+Num++` 只对已经是标题的行生效，普通文本行保持不变。任何标题再次执行同级命令都会取消标题并保留正文。光标紧跟在行首标题前缀后时，一次 Backspace 也会删除完整前缀。
- 引用命令在空行生成 `> `，执行后不会保留自动选区。
- `Tab` 优先跳出括号、引号或 Markdown 强调标记；未触发跳出时，无论光标位于行内何处，都在行首增加 4 个空格。`Shift+Tab` 减少一级缩进。
- 括号、引号、行内代码与围栏代码支持自动补全，包含半角、全角及常用中文成对符号。
  光标位于行中时，引号类符号（`` ` ``、`"`、`'`、`“”`、`‘’` 等）只输入单个开符号，
  输入闭符号完成包裹后再收尾：包裹内容含中文时 ASCII 引号转为全角；全角引号包裹
  会在与相邻中文/字母数字之间补自动空格（如 `中文 “内容” 结束`），纯 ASCII 引号包裹保持原样；
  行尾的 CJK 引号自动补全同样补空格（如 `中文` 后输入 `"` 得到 `中文 “”`）。
- 自动空格与触发它的输入动作合并为一次撤销（一次 `Ctrl+Z` 同时撤销输入与空格整理）。
- `·`（U+00B7 中间点）空格后输入时触发反引号转换：删除空格与 `·` 后生成反引号对
  `` `|` `` 并补自动空格；连续输入两个 `·`（紧贴字符）同样生成反引号对，光标居中并按
  边界规则补两侧自动空格；完全空行上生成反引号对后再输入 `·` 会升级为大代码块围栏；
  有选区时输入 `·` 等价于输入 `` ` ``，用反引号对包裹选区并触发自动空格；
  围栏代码块内不触发上述转换（一次 `Ctrl+Z` 撤销）。
- 空 Markdown 标记对与空围栏代码可用一次退格整体删除；`……`、`——` 也支持整体退格删除，空行输入 `-` 会自动补为 `- `。
- Enter 自动接续无序列表、有序列表和任务复选框（新任务重置为未勾选），并维护后续同层有序列表编号；空列表项再次 Enter 会退出列表但保留空行，Backspace 则会整行删除并回到上一行末尾。Shift+Enter 和围栏代码块内仍为普通换行。
- `Ctrl+L` 切换光标所在行的 checkbox 勾选状态；非 checkbox 行会转换为未勾选任务，并保留已有列表编号、缩进和多层引用前缀。
- 光标已经位于文档最后一个可视行时，Down 会转到行尾；位于第一个可视行时，Up 会转到行首。

## 主题与 Markdown 样式配置

配置模板集中保存在 `config/markdown-style.json`。其中 `theme.accentColor` 是界面强调色的单一事实
来源：设置页、命令面板、焦点边框、文本选区、拖动选区的落点光标和 Markdown 链接都使用该颜色；
`theme.accentTextColor` 控制强调色背景上的文字。行内代码和围栏代码的背景色、等宽字体及其他
Markdown 样式也由该文件管理。

稳定安装首次构建时会把模板初始化到
`%LOCALAPPDATA%\ScratchEditor\ScratchEditor\markdown-style.json`。Codex/pi 与 AHK 两个安装副本
共同读取并监听这一个用户配置；可随时手工保存修改，运行中的编辑器会自动热更新，无需重启。后续构建
不会覆盖该用户文件。测试模式继续读取对应构建目录中的模板，也可用
`SCRATCHEDITOR_MARKDOWN_STYLE` 指定隔离配置。

## 架构与目录

```text
KeysRedirect.ahk ──命名管道──> %LOCALAPPDATA%\ScratchEditor\AhkEditor\ScratchEditor.exe
Codex / pi ───────文件模式───> %LOCALAPPDATA%\ScratchEditor\CodexEditor\ScratchEditor.exe
共享主题配置 ────────────────> %LOCALAPPDATA%\ScratchEditor\ScratchEditor\markdown-style.json
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

在不覆盖 `build/release` 的情况下进行隔离验证，可使用：

```powershell
./scripts/build.ps1 -Preset window-ui -SkipLocalInstall
```

隔离验证 preset 按职责命名：`editing` 覆盖 Markdown、高频编辑命令、查找替换与快捷键；
`window-ui` 覆盖设置、主题、窗口交互与动画。两组验证相互独立，完整回归时都应执行。

也可以直接使用 CMake：

```powershell
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --preset release
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --build --preset release
```

`scripts/build.ps1` 会构建所选 preset、运行 `windeployqt`，并自动把刚构建的主程序同步安装到
`%LOCALAPPDATA%\ScratchEditor\CodexEditor` 与 `%LOCALAPPDATA%\ScratchEditor\AhkEditor`。前者由
Codex 和 pi 共用，后者供 AHK 常驻实例使用。构建脚本会在更新 AHK 副本前先核对运行路径，再通过专用
IPC 命令让稳定常驻实例自行退出，并在安装后重新启动；这也兼容 AHK 启动的高权限进程。只有明确传入
`-SkipLocalInstall` 才会跳过这两个本机副本；直接运行 CMake 也不会执行本机同步。

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

生产 IPC 另提供只读的 `getWindowGeometry` JSON 命令，供外部编辑进程查询常驻实例的
窗口 resting 几何，以便唤起时避开可能已打开的临时编辑器窗口。

正式配置存放在 Qt `AppConfigLocation` 下的 `settings.ini`。首次创建集中配置时会迁移
旧 Native Settings 中的窗口几何和快捷键；测试通过独立环境变量使用临时 INI。

## CLI 外部编辑器

文件位置参数会进入独立的外部编辑模式；`--wait` 是便于环境变量表达的兼容选项，进程本身
始终等待到编辑完成：

```powershell
./build/release/ScratchEditor.exe --wait ./prompt.md
```

文件模式读取和写回 UTF-8，绕过剪贴板、常驻单实例转发与生产 IPC。`Ctrl+S` 与
Escape 一样先保存并关闭本次编辑，成功后以退出码 `0` 结束；`Ctrl+W` 不保存任何编辑，
文件保持打开前的原样，同样以退出码 `0` 结束。保存失败时窗口保持打开；若外部文件已
被清理（唤起它的终端已关闭），则静默退出且不保存。

外部提示词编辑器独立记忆窗口大小（不记忆位置）；每次唤起时使用进程初始化时的前台窗口快照
定位调用它的终端并就近摆放，按当前屏幕布局和各屏 DPI 校正（热插拔后延迟到布局稳定，再重新
关联实际屏幕、以真实 resize 强制 Qt Quick 按新 DPI 重建渲染目标，并在屏幕恢复时回到拔屏前
所在的屏幕），并尽量减少与已打开临时编辑器窗口的重叠。窗口标题会标注调用它的 CLI 类型
（Codex / pi / Claude Code 等），便于区分多个外部编辑实例。

首次安装或手动刷新所有集成，可运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1
```

安装脚本从 `build/release`（或 `-SourceEditorPath` 指定的刚构建产物）同步两个稳定目录并分别部署 Qt
运行库，同时配置 Windows 用户环境变量、Git Bash、VS Code、pi 和 `KeysRedirect.ahk`。Codex 与 pi
共享同一个 `CodexEditor` 安装副本，但每次 `--wait` 编辑仍启动独立的文件模式进程，避免并发会话互相
覆盖；AHK 使用并列的 `AhkEditor` 常驻副本。配置后需要重新打开 Git Bash 并重启已运行的 Codex/pi，
因为已有进程不会重新读取环境变量或设置。

日常更新只需运行 `scripts/build.ps1`：每次成功构建都会自动同步两个稳定副本并刷新全部集成。首次部署
同样只需运行以下命令，无需随后再单独执行安装脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release
```

脚本会生成 `docs/codex-editor-installation.local.md`，集中记录这台机器的构建来源、实际部署目录、
环境变量命令和更新步骤。该文件包含本机路径，已加入 `.gitignore`；每次通过构建脚本同步或手动执行
`-Action Install` 都会自动刷新。通用检查命令为：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1 -Action Check
```

外部编辑器按终端环境区分：VS Code 集成终端中的 `Ctrl+G` 使用 `code --wait`，普通终端使用稳定部署的
ScratchEditor。Git Bash 通过 `TERM_PROGRAM=vscode` 判断；VS Code 用户设置中的
`terminal.integrated.env.windows` 为 PowerShell、CMD 等其他集成终端注入相同变量。Codex 文件引用的
点击行为不受此切换影响，仍由全局 `file_opener = "vscode"` 统一交给 VS Code。

WSL 复用同一判定：VS Code 集成终端自带 `TERM_PROGRAM=vscode`，因此 `Ctrl+G` 使用 `code --wait`；
其他 WSL 终端使用 [`scripts/configure-codex-editor-wsl.sh`](scripts/configure-codex-editor-wsl.sh) 安装的
包装脚本，先 `wslpath -w` 转换临时文件路径，再调用 C 盘稳定版 `CodexEditor\ScratchEditor.exe --wait`
并回传退出码。`configure-codex-editor.ps1 -Action Install`（含每次 `build.ps1`）检测到 WSL 时会自动同步，
也可在 WSL 中手动运行 `bash scripts/configure-codex-editor-wsl.sh install` 或 `check`。
不需要 WSL 同步时，可在调用 `configure-codex-editor.ps1` 时附加 `-SkipWslSync`。
WSL 侧暂不执行 `file_opener = "vscode"` 的 Windows 对齐；该选项只影响可点击文件引用，不影响
`Ctrl+G` 外部提示词编辑器。

Codex 在 composer 中按 `Ctrl+G`；安装脚本会自动把已检测到的 pi `settings.json` 指向同一个稳定副本：

```json
{
  "externalEditor": "C:\\Users\\<用户名>\\AppData\\Local\\ScratchEditor\\CodexEditor\\ScratchEditor.exe --wait"
}
```

将 `<用户名>` 替换为实际 Windows 用户目录名；也可复制本机安装文档中的 `VISUAL / EDITOR` 值。

三种 CLI 的调用约定：Codex 在 composer 中按 `Ctrl+G`，依次读取 `VISUAL`、`EDITOR`；pi 优先使用
`externalEditor`，再回退到 `VISUAL`/`EDITOR`（Windows 最终回退到 Notepad），并在编辑器子进程以
退出码 `0` 结束时读回；Claude Code 的 `Ctrl+G` 打开系统配置的默认文本编辑器。因此 `VISUAL` 与
`EDITOR` 应统一指向同一个 `--wait` 命令；Git Bash 中的 `export VISUAL=...` 会覆盖 Windows 用户
环境变量，安装脚本维护的 `~/.bashrc` 与 VS Code 终端注入会保证两侧一致。此集成不需要 Windows
注册表、文件关联、插件、MCP 或智能体工具。

当前已在原生 Windows 上实测 Codex CLI 0.146.0 与 pi 0.80.10 的完整 `Ctrl+G` 等待、写回
和返回流程。Claude Code 按本轮范围暂未实测；官方约定见
[Codex CLI Prompt editor](https://learn.chatgpt.com/docs/cli-customization#prompt-editor)、
[pi settings](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/settings.md#ui--display)
与 [Claude Code interactive mode](https://code.claude.com/docs/en/interactive-mode#general-controls)。
历史调查与扩展计划归档在 [`docs/archive/001-external-editor/`](docs/archive/001-external-editor/)。

## 验收

外部文件核心、进程生命周期、并发会话与常驻实例隔离：

```powershell
./scripts/run-external-editor-tests.ps1
node ./scripts/run-external-cli-integration.mjs
```

第二条命令是本机 CLI 级联调，使用 ConPTY，并要求存在 `node-pty`；可通过
`SCRATCHEDITOR_NODE_PTY` 指向已安装的包目录。测试使用隔离的 Codex/pi 临时配置，写回
`/quit` 后退出，不发送模型请求，也不修改用户的 CLI 配置。

AHK 迁移安装状态、备份、IPC、失败回退和进程保护验收：

```powershell
./scripts/run-ahk-tests.ps1
```

Qt 功能回归分为独立的编辑行为验证与窗口界面验证；两者的 AHK 基线参数都应指向迁移
备份：

```powershell
./scripts/run-editing-tests.ps1 `
  -BuildSubdirectory build\editing `
  -OriginalAhkPath D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834

./scripts/run-window-ui-tests.ps1 `
  -BuildSubdirectory build\window-ui `
  -OriginalAhkPath D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834
```

窗口界面入口还会检查四角缩放、边框拖动和编辑区域配色分层，并连续执行 20 轮唤出/关闭，
确认隐藏态几何稳定、窗口不会在关闭前回弹且再次唤出后恢复到记录尺寸。

完整性能回归：

```powershell
./scripts/run-perf-tests.ps1 `
  -BuildSubdirectory build\window-ui `
  -ServerName ScratchEditor.Validation.Perf `
  -ArtifactPrefix validation-performance
```

所有当前测试使用独立管道和测试配置，不会停止默认管道上的用户实例。详细说明见
[tests/README.md](tests/README.md)。

AHK 迁移最终实测：冷启动最大 75.18 ms、热唤醒 P95 19.54 ms、10 万字输入到帧
P95 16.61 ms、空闲 CPU 0%、工作集 39.61 MB、动画 59.88 FPS；微软拼音精确提交
`你好`。完整 JSON 证据保存在 [artifacts/baselines](artifacts/baselines/README.md)。

## AHK 迁移边界

`integration/KeysRedirect.QtMigration.ahk` 是迁移早期的历史隔离参考副本，仍保留当时的
Qt/旧 GUI 回退开关，不会被构建或测试脚本自动安装。AHK 迁移当前状态：

- 原文件备份：`D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834`。
- 备份 SHA-256：`8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9`。
- 当前 `KeysRedirect.ahk` 的 Qt 回退路径由安装脚本维护，固定指向
  `%LOCALAPPDATA%\ScratchEditor\AhkEditor\ScratchEditor.exe`。
- 每次 `scripts/build.ps1` 成功构建都会更新该稳定副本；若稳定常驻实例正在运行，安装脚本会先隐藏并
  停止该确切实例，安装完成后再重新启动。

Qt 部署资源目前会输出已知的 `libpng iCCP` 警告，不影响功能、像素检查或性能验收。
