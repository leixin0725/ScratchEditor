# 运行模式与集成

```text
KeysRedirect.ahk ──命名管道──> %LOCALAPPDATA%\ScratchEditor\AhkEditor\ScratchEditor.exe
Codex / pi ───────文件模式───> %LOCALAPPDATA%\ScratchEditor\CodexEditor\ScratchEditor.exe
共享 Markdown 主题配置 ──────> %LOCALAPPDATA%\ScratchEditor\ScratchEditor\markdown-style.json
共享 UI 设计令牌配置 ────────> %LOCALAPPDATA%\ScratchEditor\ScratchEditor\ui.json
                                             ├─ C++：应用协调、IPC、配置、编辑命令、窗口过渡
                                             │       └─ ClipboardHistoryCoordinator：剪贴板访问、历史领域与持久化
                                             └─ QML：编辑器、查找、命令面板、设置页与界面动效
```

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

`EditorController` 负责解析、ready/test 门禁、生产命令和回复封装。只有 test-mode
实例才创建私有的 `TestSupport` 并注册测试命令。测试诊断字段、像素采样、输入帧与
动画基准的待回复状态在该对象中维护。生产和测试复用同一个分发入口。

生产 IPC 另提供只读的 `getWindowGeometry` JSON 命令，供外部编辑进程查询常驻实例的
窗口 resting 几何，以便唤起时避开可能已打开的临时编辑器窗口。

## 剪贴板历史

剪贴板历史仅在普通常驻模式启用。实例启动后通过 Windows 剪贴板监听消息捕获
`CF_UNICODETEXT` 纯文本；启动前已有内容、空内容、非文本、读取失败、UTF-8 编码后超过
1 MiB 的单项，以及带 `ExcludeClipboardContentFromMonitorProcessing` 或明确禁止
`CanIncludeInClipboardHistory` 标记的内容都不会进入历史。集合最多保留 100 条，按全文逐个
UTF-16 code unit 精确去重；再次复制完全相同的文本会保留稳定 ID、刷新时间并移动到最前。

编辑器左侧内沿提供 12px 触发区；窗口左框还有不拦截拖动和缩放的 hover 感应区，从外侧靠近并
停留 100ms 可展开，指针快速向左越出窗口时立即展开；指针离开触发区和面板后停留 500ms 自动
收起。也可从命令面板执行“切换剪贴板历史”，
再次执行（或按已配置快捷键）即可关闭。
该命令默认不绑定快捷键，可沿用通用快捷键设置自行配置。面板支持全文大小写不敏感搜索、鼠标双击或键盘 Enter
载入；若当前编辑缓冲区相对打开/上次载入基线有修改，会先请求确认。历史原项保持不可变，之后
仍可用 Escape、`Ctrl+S`、`Ctrl+W` 按现有语义回写或放弃。删除单项和确认清空只改变内部历史，
不写剪贴板，也不改变当前编辑文本。

按住历史卡片并移动超过系统拖动阈值后，可把该项完整文本插入编辑区的鼠标落点。拖动期间显示
与选区移动相同的落点光标；有效落点使用复制光标，无效区域使用禁止光标，同时显示跟随指针的
半透明卡片摘要。摘要限制在窗口内且最多三行，不遮住指针热点。靠近视口上下边缘时自动滚动；
历史面板保持展开，避免编辑区在拖动途中改变几何。成功放置后新增文本保持选中、编辑器重新获得
焦点并关闭历史面板，整个插入（包括必要的有序列表编号维护）可用一次 `Ctrl+Z` 撤销；
撤销后恢复拖入前的光标，原有选区则连同活动端方向一并恢复。放到编辑区
之外只会取消操作，不会修改正文或历史原项；未超过拖动阈值的按放仍是普通单击，也不会显示预览。

历史卡片默认高度为 58 像素，可在 `settings.ini` 的 `[clipboardHistory]` 段以 `cardHeight` 键调整
（合法范围 44–200，修改后重启生效）；该键属于文件级配置，不进入设置页。
鼠标悬停卡片时会显示中性背景高亮，但不会改变当前选中项；单击后才会更新选择。
历史面板打开时，左上、左下两个内部圆角默认使用专用的 8px 半径，以清晰露出边缘；右侧与
正文编辑区保持直线拼接。该半径可通过 `config/ui.json` 的 `panels.history.cornerRadius` 调整。
面板标题与主窗口标题共用左边缘和垂直中心；搜索框顶部与右侧编辑视口顶部对齐，
下方列表与搜索框、页脚按钮分别保持 8px 和 10px 间距。

历史文件与 `settings.ini` 同目录，正式环境通常为
`%LOCALAPPDATA%\ScratchEditor\ScratchEditor\clipboard-history.dat`。文件使用当前 Windows 用户
范围的 DPAPI 加密，并以带版本、长度与 SHA-256 完整性校验的二进制 envelope 保存；这可以避免
磁盘上的直接明文，但同一登录用户上下文仍可通过 DPAPI 解密。写入通过原子替换保留最近一次有效
文件；读取或解密失败时进入 `ReadLocked`，不会自动覆盖原文件，只有用户明确确认清空后才重置。
保存失败时会保留本次会话内存结果并显示错误，磁盘上的 last-known-good 不变。

`--wait <path>` 外部文件模式不会创建模型、注册监听器、读取历史文件或注册历史命令。测试模式
统一使用内存剪贴板 gateway 和隔离的 settings/history 路径，生产 IPC 不开放任何历史测试命令。

## CLI 外部编辑器

便携版可手动配置到固定解压路径后供 CLI 使用。以下 PowerShell 环境变量只影响当前 shell
及其启动的子进程，不会写入 Windows 用户环境设置：

```powershell
$env:VISUAL = '"C:\Tools\ScratchEditor\ScratchEditor.exe" --wait'
$env:EDITOR = $env:VISUAL
```

在同一个 shell 中启动 Codex 或 pi 即可使用该编辑器。pi 也可以在自己的 `settings.json` 中将
`externalEditor` 设置为带 `--wait` 的可执行文件命令。移动或删除解压目录前，应先更新或移除这些
手动设置。便携包不包含 AutoHotkey 快捷键配置。

下方的自动集成配置脚本仅供从源码构建的开发环境使用，会更新本机环境变量和检测到的外部集成；
它不属于公开 ZIP，也不会由 WinGet 或公开发行工作流调用。

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
其他 WSL 终端使用 [`scripts/configure-codex-editor-wsl.sh`](../../scripts/configure-codex-editor-wsl.sh) 安装的
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

## AHK 迁移边界

`integration/KeysRedirect.QtMigration.ahk` 是迁移早期的历史隔离参考副本，仍保留当时的
Qt/旧 GUI 回退开关，不会被构建或测试脚本自动安装。AHK 迁移当前状态：

- 原文件备份：`D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834`。
- 备份 SHA-256：`8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9`。
- 当前 `KeysRedirect.ahk` 的 Qt 回退路径由安装脚本维护，固定指向
  `%LOCALAPPDATA%\ScratchEditor\AhkEditor\ScratchEditor.exe`。
- 未指定 `-SkipLocalInstall` 时，成功构建都会更新该稳定副本；若稳定常驻实例正在运行，安装脚本会先隐藏并
  停止该确切实例，安装完成后再重新启动。

Qt 部署资源目前会输出已知的 `libpng iCCP` 警告，不影响功能、像素检查或性能验收。
