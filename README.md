# ScratchEditor 用户指南

ScratchEditor 是一款面向 Windows 的轻量临时文本编辑器，适合快速整理剪贴板文本、
编写 Markdown，以及在 Codex、pi-coding-agent 等命令行工具中编辑较长的提示词。
它以速度和键盘操作为重点：窗口可常驻后台并通过全局快捷键即时唤出，也可以按文件
启动为一次性的外部编辑器。

ScratchEditor 不是完整的 IDE 或笔记管理工具。它不提供 Markdown 预览、标签页、固定草稿、
多光标、插件或 LSP，也不包含 WebEngine、WebView 等浏览器内核。

## 适合用来做什么

- 临时修改剪贴板中的一段文字，再复制或直接输入回原来的应用。
- 快速编写带标题、列表、任务项、引用和代码块的 Markdown 文本。
- 浏览、搜索和重新使用本机最近复制过的纯文本。
- 在 Codex 或 pi-coding-agent 中按 `Ctrl+G` 打开独立窗口编辑提示词。
- 通过 `--wait <path>` 为其他命令行程序提供同步的 UTF-8 文件编辑器。

窗口支持置顶、自动换行、原生拖动与缩放、高 DPI、多屏和深浅主题。中文输入方面针对
微软拼音、CJK 词边界、中文标点和常用成对符号做了专门适配。

## 安装与更新

ScratchEditor 的首次公开发行面向 Windows 11 x64，提供便携 ZIP，并在 WinGet 清单审核通过后提供命令行安装。公开发行件会发布在
[GitHub Releases](https://github.com/leixin0725/ScratchEditor/releases/latest)。

### 便携 ZIP

下载 `ScratchEditor-<版本>-windows-x64.zip`，解压后运行 `ScratchEditor.exe`。Qt 运行库已随包提供，
不需要单独安装 Qt 或开发工具。发行包没有数字签名，Windows 可能显示未知发布者提示；可用同一发行页
提供的 SHA-256 文件核对下载内容：

```powershell
Get-FileHash .\ScratchEditor-<版本>-windows-x64.zip -Algorithm SHA256
```

用户设置和剪贴板历史继续保存在 `%LOCALAPPDATA%\ScratchEditor\ScratchEditor`（Qt `AppConfigLocation`），不随解压目录移动。
首次运行会按现有行为生成用户配置。升级时先退出应用，将新版本解压到新目录再运行；确认正常后可删除旧目录。
删除解压目录不会删除这些数据。便携包默认不会改动 Codex、pi、AutoHotkey、WSL、Git Bash、VS Code
或用户环境变量。

### WinGet

WinGet 社区清单合并后，可安装公开版本：

```powershell
winget install --id leixin0725.ScratchEditor --exact
winget upgrade --id leixin0725.ScratchEditor --exact
winget uninstall --id leixin0725.ScratchEditor --exact
```

WinGet 自身管理命令入口，必要时将其 Links 目录加入用户 PATH；这是经允许的安装行为。
应用及发行脚本不配置外部工具。卸载 WinGet 包应保留用户设置和剪贴板历史，发行前需在干净环境验证。

### 源码构建

开发者可从源码构建并部署本机集成。首次使用时，在项目根目录打开 PowerShell；如果 `.tools/Qt` 中还没有项目工具链，先运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\restore-toolchain.ps1
```

然后构建并部署开发者本机使用的稳定副本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release
```

该命令会生成并同步两个用途隔离的本机副本：

- `%LOCALAPPDATA%\ScratchEditor\AhkEditor`：供 AutoHotkey 全局快捷键调用的常驻编辑器。
- `%LOCALAPPDATA%\ScratchEditor\CodexEditor`：供 Codex 和 pi 使用的外部文件编辑器。

构建脚本还会刷新已检测到的 AutoHotkey、Codex、pi、Git Bash、VS Code 和 WSL 集成；
此行为只适用于开发者本机部署，不属于公开 ZIP 或 WinGet 安装流程。
首次配置后，应重新打开 Git Bash，并重启正在运行的 Codex 或 pi，使它们重新读取环境变量
和设置。以后更新项目时，重新执行同一条 release 构建命令即可。

完整工具链和本机集成检查命令见[构建与发行](docs/build-release/README.md)。

## 三种使用方式

### 临时剪贴板编辑器

安装并运行配套 AutoHotkey 脚本后，可通过已配置的全局快捷键（当前集成使用
`Scroll Lock`、`Win+F` 等入口）唤出常驻编辑器。窗口打开时会载入纯文本剪贴板内容，
光标默认位于文档末尾；再次触发切换命令可隐藏窗口。关闭后，焦点会交还最近使用的窗口。

常驻模式中的三个关闭操作语义不同：

| 操作 | 结果 |
|---|---|
| `Escape` | 把当前文本写入剪贴板，然后关闭窗口。 |
| `Ctrl+S` | 把当前文本写入剪贴板、关闭窗口，并将文本输入到随后获得焦点的窗口。 |
| `Ctrl+W` | 放弃本次修改并关闭窗口；恢复到打开或载入前的状态，不改写剪贴板。 |

如果读取或写入剪贴板失败，编辑器会保留窗口与内容并显示错误，避免静默丢失编辑结果。

### CLI 外部提示词编辑器

完成集成配置后，在 Codex 或 pi 的输入区按 `Ctrl+G`，即可用独立的 ScratchEditor 窗口
编辑当前提示词。保存并关闭后，CLI 会继续运行并读回修改后的内容。多个 CLI 会话分别使用
独立进程，不会互相覆盖。

在这种文件模式下：

- `Escape` 或 `Ctrl+S` 保存 UTF-8 文件并关闭，进程以退出码 `0` 结束。
- `Ctrl+W` 放弃所有修改并关闭，原文件保持不变。
- 保存失败时窗口不会关闭，可修正问题或复制内容后再处理。
- 窗口标题会标出 Codex、pi、Claude Code 等调用来源；不同来源的实际支持程度见 [CLI 外部编辑器](docs/integration/README.md#cli-外部编辑器)。

VS Code 集成终端中的 `Ctrl+G` 默认使用 `code --wait`；普通 Windows 终端和已配置的 WSL
终端使用 ScratchEditor。这一差异由安装脚本自动配置。

### 直接编辑文件

也可以手动把 ScratchEditor 作为同步外部编辑器调用：

```powershell
& "$env:LOCALAPPDATA\ScratchEditor\CodexEditor\ScratchEditor.exe" --wait .\prompt.md
```

该模式直接读取和写回指定的 UTF-8 文件，不使用系统剪贴板、不连接常驻实例，也不读取
剪贴板历史。调用进程会等待编辑窗口关闭。

## 日常编辑

ScratchEditor 支持标准的输入、选择、复制、粘贴、撤销和重做，同时对临时写作做了以下增强：

- 自动高亮 Markdown 标题、强调、引用、任务项、链接、行内代码和围栏代码。
- 悬停 Markdown 链接时高亮整段语法；按住 `Ctrl` 单击 HTTP/HTTPS 链接可用默认浏览器打开。
- 自动补全括号、引号、行内代码标记和围栏代码，并让空标记对可以一次退格删除。
- 在按 Enter 时接续无序列表、有序列表、任务项和引用；有序列表会自动维护同层编号。
- 可从命令面板把选中行或当前行的普通无序列表转换为有序列表；混合选区会并入完整的
  有序列表并统一重新编号。
- 针对连续中文、日文假名、韩文谚文及中英文混排优化按词移动、选择和删除。
- 根据上下文转换中文标点，并在中文与英文或标记之间整理必要的空格。
- 用方向键移动光标或扩展选区时，正文会以最小步幅保持活动端可见。
- 按住鼠标把新选区拖出正文边缘时会持续自动滚动；也可直接拖动已有文本选区，
  边缘滚动后的整次移动可单步撤销。
- 可把一个或多个本地文件、文件夹拖入正文，在当前光标处插入绝对路径；已有选区时替换
  选区，多项按拖入顺序用单个空格连接，含空白字符的路径会用双引号包裹。路径与相邻正文
  之间不会自动补空格，整次插入可单步撤销。该功能同时适用于常驻模式和 `--wait` 文件模式。
- `PageUp` 和 `PageDown` 只滚动页面，不移动光标或改变选区；正文末尾保留额外阅读留白。

### 编辑快捷键

以下是源码内置默认值；所有命令均可在命令面板中修改快捷键，用户配置可能与此不同。

| 命令 | 默认快捷键 |
|---|---:|
| 加粗 / 斜体 | `Ctrl+B` / `Ctrl+I` |
| 设为 1–6 级标题 | `Ctrl+Num+1`–`Ctrl+Num+6` |
| 标题向 6 级 / 1 级推进 | `Ctrl+Num+-` / `Ctrl+Num++` |
| 折叠 / 展开所有标题 | `Ctrl+M` / `Ctrl+Shift+M` |
| 折叠 / 展开所属标题 | `Ctrl+Shift+[` / `Ctrl+Shift+]` |
| 跳到上一个 / 下一个标题 | `Ctrl+Up` / `Ctrl+Down` |
| 删除整行 | `Ctrl+Shift+L` |
| 清空整个编辑区 | `Alt+X` |
| 无选区复制 / 剪切整行 | `Ctrl+C` / `Ctrl+X` |
| 无选区粘贴整行为下方新行 | `Ctrl+V`（智能） |
| 切换任务项 / 切换本行 checkbox | `Ctrl+Alt+T` / `Ctrl+L` |
| 切换引用 / 切换代码标记 | `Ctrl+Shift+Q` / `Ctrl+Alt+C` |
| 查找 / 替换 | `Ctrl+F` / `Ctrl+H` |
| 命令面板 / 设置 | `Ctrl+Shift+P` / `Ctrl+,` |
| 转换为有序列表 / 切换剪贴板历史 / 循环标题级别 / 切换项目列表 | 无默认快捷键 |


没有选区时，`Ctrl+C` 和 `Ctrl+X` 会处理光标所在的整行；如果剪贴板文本以换行结尾，
`Ctrl+V` 会把它作为新行插入到当前行下方。有选区时，这三个快捷键保持标准行为。
完整的输入规则和边界情况见开发者文档中的[命令与撤销边界](docs/editor/README.md#命令与撤销边界)。

### 查找与替换

`Ctrl+F` / `Ctrl+H` 打开查找／替换面板，面板在正文上方占用独立空间，不遮挡首行。
正文有单行选区时，面板会把选区原样填入查找框；跨行选区不会覆盖已有查找内容。
查找／替换面板的左右外边缘始终与编辑区域对齐，上边缘与面板关闭时的正文顶部对齐。
剪贴板历史栏处于推挤模式时，面板会随历史栏的开合动画同步移动和收窄；窄窗口的历史栏
覆盖模式保持不变。
搜索框中按 Enter 查找下一个，Shift+Enter 查找上一个，也可使用面板按钮或 F3／Shift+F3；
查找后搜索框保持焦点，正文匹配保持选中。只要面板可见，焦点无论位于搜索框、替换框、
正文还是其他控件，按 Escape 都会关闭面板并回到正文。

匹配完整可见时不滚动；位于视口外时平滑定位到正文区上方约三分之一，首尾按滚动范围限制。
超过一屏的匹配优先显示起始行；折叠正文会先展开再定位。动画关闭时直接到位，手动滚动会取消
尚未完成的查找定位。即使文档只有一个匹配，手动滚走后再次查找也会重新显示它。

“替换当前”在选区匹配时替换并选中新文本，否则先查找下一处；不会在替换后自动跳到下一处。
全部替换保持一次批量操作，不逐项滚动，结束后仅在必要时显示最终光标或选区。

## 剪贴板历史

剪贴板历史仅在普通常驻模式启用。把鼠标停在编辑器左侧内沿即可展开历史面板，也可以打开
命令面板并执行“切换剪贴板历史”。该命令默认没有快捷键，需要时可在设置中自行绑定。

搜索框持有焦点或输入法正在组词时，历史栏保持展开；焦点离开且鼠标位于栏外后恢复延迟收起。

在面板中可以：

- 输入关键词进行全文、不区分大小写的搜索。
- 双击历史卡片，或选中后按 Enter，将其载入编辑区。
- 把历史卡片拖到编辑区的指定位置；拖动时会显示复制/禁止光标和跟随指针的半透明摘要，
  插入完整内容后可用一次 `Ctrl+Z` 撤销，并恢复拖入前的光标或选区及其方向。
- 删除单项，或在确认后清空全部历史。
- 编辑载入的历史文本，再按正常的保存或放弃语义处理；原历史项不会被同步改写。

如果当前编辑区已有未保存修改，载入另一条历史前会要求确认。删除或清空历史不会改写当前
剪贴板，也不会改变正在编辑的文本。

历史最多保留 100 条启动后捕获的纯文本，完全相同的文本会去重并移到最前。空内容、非文本、
超过 1 MiB 的单项，以及明确禁止进入系统历史的内容不会被记录。历史文件通常位于：

```text
%LOCALAPPDATA%\ScratchEditor\ScratchEditor\clipboard-history.dat
```

文件使用当前 Windows 用户范围的 DPAPI 加密并带完整性校验，可避免直接以明文落盘；但同一
Windows 登录用户上下文仍有能力解密，因此不要把它当作抵御本机账户入侵的保险库。读取或解密
失败时，编辑器不会自动覆盖原文件，只有在用户明确确认清空后才会重置。

## 外观、设置与配置

按 `Ctrl+,` 打开设置页，可调整深浅主题、编辑区 ASCII 主字体、中文 fallback 字体、字号、
九档字体粗细（100–900）、动画开关、状态面板显示方式，
以及各编辑命令的快捷键。窗口尺寸会自动记忆；临时剪贴板编辑器与 CLI 外部编辑器分别保存
自己的窗口几何。

普通用户设置保存在 Qt 应用配置目录下的 `settings.ini`。主题和界面模板首次安装后会初始化为：

```text
%LOCALAPPDATA%\ScratchEditor\ScratchEditor\markdown-style.json
%LOCALAPPDATA%\ScratchEditor\ScratchEditor\ui.json
```

`markdown-style.json` 控制 Markdown 配色、界面强调色和代码背景，保存后会自动
热更新；`ui.json` 控制窗口、布局、面板和动画等设计参数，修改后需要重启应用。后续构建不会
覆盖已经存在的用户副本。字段和合法取值见 [`config/README.md`](config/README.md)。

编辑区默认按 `Consolas → NSimSun` 的有序字体链渲染：ASCII 优先使用 Consolas，Consolas
缺失的汉字和全角标点回退到 NSimSun；Consolas 已提供的弯引号等字形不会强制切换字体。
正文与全部 Markdown 高亮区域共用设置页中的字体链和字号。Markdown 字重由
`markdown-style.json` 的 `fontWeightDelta` 相对正文粗细计算：标题、粗体和粗斜体默认增加
300，其余样式默认增加 0，最终结果限制在 100–900；默认正文粗细仍为常规（400）。

## 常见问题

**全局快捷键没有反应**

先运行集成检查命令，确认 `AhkEditor` 副本、AutoHotkey 路径和相关配置有效。如果常驻实例
以高于当前终端的权限运行，普通权限脚本无法控制它；请从管理员 PowerShell 重新执行检查。

**Codex 或 pi 仍然打开旧编辑器**

安装后重新启动对应 CLI，并重新打开终端，使新进程读取更新后的 `VISUAL`、`EDITOR` 或
`externalEditor` 设置。VS Code 集成终端按设计会使用 `code --wait`。

**剪贴板历史显示读取错误**

原历史文件会被保留而不会自动覆盖。可先点击右上角错误信息复制详情；只有确定不再需要原历史时，
才在界面中确认清空并重置文件。

**为什么看不到 Markdown 预览**

ScratchEditor 只提供原生语法高亮和编辑辅助，不提供渲染预览。这是明确的产品边界。

## 开发文档

[开发导航与文档管理规则](docs/README.md)包含模块边界、隔离构建、回归顺序和当前调查。
