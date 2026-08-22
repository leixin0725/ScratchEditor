# ScratchEditor

ScratchEditor 是从 AutoHotkey 临时编辑器迁移出的轻量 Windows 编辑器。主程序使用
Qt 6 Quick/QML、C++20 和 CMake；AutoHotkey 继续负责全局快捷键与启动调度，并通过
本地命名管道控制常驻的 `ScratchEditor.exe`。

迁移阶段 1–6 已完成。经用户明确批准并先创建同目录备份后，原
`D:\Documents\AutoHotkey\KeysRedirect.ahk` 已移除旧 GUI，只保留快捷键、Qt IPC
调度和启动失败时不改写内容的纯剪贴板回退。外部 AHK 文件和备份不属于本仓库。

## 功能

- 单实例常驻窗口，支持 `toggle`、`show`、`hide` 本地 IPC。
- 剪贴板载入（新内容光标默认落在文档末尾）/回写、Escape 关闭并复制、Ctrl+S 关闭并把内容输入到下一个窗口、Ctrl+W 关闭且不保存（完全回退到打开前状态）、关闭后焦点交给最近活跃窗口（外部 CLI 模式除外）和剪贴板异常保护。
- 无标题置顶窗口、四边/四角原生缩放、顶部与非缩放边框区域拖动、窗口几何记忆（临时编辑器与外部提示词编辑器独立）、自动换行和智能滚动条。
- `PageUp`/`PageDown` 按一页纯滚动浏览（光标与选区不动）；正文下方保留 2/3 页可滚动的留白区；输入导致光标碰到视口底边时自动滚动一次：段中光标行滚到视口上 1/3，段尾等效于滚到底（正文末尾下方 2/3 页留白翻出，光标停在上 1/3）；之后继续输入不再重复触发，光标自然下落，再次触底才再次触发（间歇式）。删除/剪切/撤销等使光标越过视口顶边时按严格镜像规则自动滚动：光标行滚到视口距顶 2/3 处，文档开头滚到顶部，同样间歇式触发；撤销与重做视为普通输入/删除，共用同一套自动滚动检查。以上翻页与自动滚动在动画开关开启时使用约 160ms 的轻量平滑滚动，关闭时瞬时到位。
- CJK 字体、微软拼音、高 DPI 和暗色首帧保障。
- 原生 Markdown 语法高亮与常用 Markdown 编辑命令。
- Markdown 标题支持层级折叠、常驻标记和上一个/下一个标题导航。
- 查找替换、延迟加载命令面板和可配置快捷键。
- 可直接拖动已有文本选区移动内容，支持跨行落点、边缘自动滚动和单步撤销。
- 延迟加载设置页、深浅主题、编辑字体/字号，以及同步透明度与居中形变的轻量唤出/关闭动画开关。
- 右上角动态状态显示：正常显示字数统计（有选区时显示选区/总数），悬停展开状态面板（按配置展示快捷键提示或红色错误详情），错误信息可点击复制；面板字号、悬停/收起延迟与最大宽度可在设置页配置。
- 配置按职责分层管理：`config/ui.json` 统一 UI/动画设计令牌（JSONC，带注释）、
  `config/markdown-style.json` 管理 Markdown 高亮与强调色，用户覆盖值保存在
  带 schema 的 `settings.ini`；详细说明见 [`config/README.md`](config/README.md)。
- 常驻模式监听启动后的纯文本剪贴板变化，并提供加密的本地历史浏览、搜索、回溯编辑、删除与清空。
- 可作为 Codex 和 pi-coding-agent 的同步外部提示词编辑器（标题标注调用它的 CLI 类型），按文件启动独立瞬态进程。

按用户决定不提供 Markdown 预览。项目也不使用 Qt WebEngine、WebView 或其他浏览器
内核；历史草稿、标签页、固定草稿、多光标、插件和 LSP 当前均不在范围内。

## 剪贴板历史

剪贴板历史仅在普通常驻模式启用。实例启动后通过 Windows 剪贴板监听消息捕获
`CF_UNICODETEXT` 纯文本；启动前已有内容、空内容、非文本、读取失败、UTF-8 编码后超过
1 MiB 的单项，以及带 `ExcludeClipboardContentFromMonitorProcessing` 或明确禁止
`CanIncludeInClipboardHistory` 标记的内容都不会进入历史。集合最多保留 100 条，按全文逐个
UTF-16 code unit 精确去重；再次复制完全相同的文本会保留稳定 ID、刷新时间并移动到最前。

编辑器左侧内沿提供 12px 触发区；窗口左框还有不拦截拖动和缩放的 hover 感应区，从外侧靠近并
停留 100ms 可展开，指针快速向左越出窗口时立即展开。也可从命令面板执行“切换剪贴板历史”，
再次执行（或按已配置快捷键）即可关闭。
该命令默认不绑定快捷键，可沿用通用快捷键设置自行配置。面板支持全文大小写不敏感搜索、鼠标双击或键盘 Enter
载入；若当前编辑缓冲区相对打开/上次载入基线有修改，会先请求确认。历史原项保持不可变，之后
仍可用 Escape、`Ctrl+S`、`Ctrl+W` 按现有语义回写或放弃。删除单项和确认清空只改变内部历史，
不写剪贴板，也不改变当前编辑文本。

历史卡片默认高度为 58 像素，可在 `settings.ini` 的 `[clipboardHistory]` 段以 `cardHeight` 键调整
（合法范围 44–200，修改后重启生效）；该键属于文件级配置，不进入设置页。
鼠标悬停卡片时会显示中性背景高亮，但不会改变当前选中项；单击后才会更新选择。
历史面板打开时，左上、左下两个内部圆角默认使用专用的 8px 半径，以清晰露出边缘；右侧与
正文编辑区保持直线拼接。该半径可通过 `config/ui.json` 的 `panels.history.cornerRadius` 调整。

历史文件与 `settings.ini` 同目录，正式环境通常为
`%LOCALAPPDATA%\ScratchEditor\ScratchEditor\clipboard-history.dat`。文件使用当前 Windows 用户
范围的 DPAPI 加密，并以带版本、长度与 SHA-256 完整性校验的二进制 envelope 保存；这可以避免
磁盘上的直接明文，但同一登录用户上下文仍可通过 DPAPI 解密。写入通过原子替换保留最近一次有效
文件；读取或解密失败时进入 `ReadLocked`，不会自动覆盖原文件，只有用户明确确认清空后才重置。
保存失败时会保留本次会话内存结果并显示错误，磁盘上的 last-known-good 不变。

`--wait <path>` 外部文件模式不会创建模型、注册监听器、读取历史文件或注册历史命令。测试模式
统一使用内存剪贴板 gateway 和隔离的 settings/history 路径，生产 IPC 不开放任何历史测试命令。

## 编辑快捷键

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
| 切换剪贴板历史 / 循环标题级别 / 切换项目列表 | 无默认快捷键 |

- 加粗、斜体和代码命令在无选区时按中英文自适应词边界处理相邻词语（连续中文整体
  包裹，中文标点切分，`英文+中文` 混合串只包裹光标所在侧的词）；在同类标记内部
  再次触发会取消对应格式，跨边界选区则只清理同类内部标记。
- 无选区时 `Ctrl+C` 复制光标所在整行（统一携带行尾换行符，末行也补上），`Ctrl+X` 剪切整行并让后续行补位、光标落在补位后一行的行首；无选区 `Ctrl+V` 在剪贴板以换行结尾时将其粘贴为当前行下方的新行（当前行原样保留，连续粘贴持续在下方堆叠；空文档、文档已以换行结尾时直接插入，不会产生前导空行），否则在光标处标准插入。有选区时这三个快捷键保持标准复制/剪切/替换行为。
- 三击鼠标选中光标所在逻辑整行（非末行含行尾换行符，末行不含），双击仍按词选择；三击后 `Ctrl+C` 复制整行内容（非末行含行尾换行符）。
- `Ctrl+左/右` 与 `Ctrl+Shift+左/右` 使用中英文自适应词边界移动/扩选：连续中文
  （含日文假名、韩文谚文与补充平面汉字）整体作为一个词一次跳过，中文标点
  （`，。！？、（）“”` 等）与空白、ASCII 标点一样作为分隔符被跳过，中文与相邻
  英文/数字边界正确切分；纯英文文本保持 Qt 原生按词移动行为。双击按词选择使用
  同一套边界：双击连续中文选中整串、双击英文单词选中该词、双击中文标点选中该标点；
  `Ctrl+Backspace` / `Ctrl+Delete` 按词删除与 Markdown 快捷键（`Ctrl+B`、`Ctrl+I`、
  `Ctrl+Alt+C`）的无选区按词包裹同样使用该边界。
- 标题设置、推进和循环命令不会主动选中当前标题行；光标会保持在正文中的相对位置。
- 空白行执行任一设置/循环标题命令会创建对应的 `# ` 到 `###### `；`Ctrl+Num+-` / `Ctrl+Num++` 只对已经是标题的行生效，普通文本行保持不变。任何标题再次执行同级命令都会取消标题并保留正文。光标紧跟在行首标题前缀后时，一次 Backspace 也会删除完整前缀。
- 标题折叠识别与语法高亮一致的 ATX 1–6 级标题，并忽略反引号围栏代码内的伪标题。可折叠标题左侧的 `v` / `>` 标记始终可见：展开时使用弱化色，折叠时使用界面强调色。折叠一个标题会隐藏其正文和下属子标题，直到下一个同级或更高级标题；“折叠所有标题”保留文档顶层大纲可见。若光标位于将被隐藏的正文，折叠后会回到最近的可见标题行首。
- `Ctrl+Up` / `Ctrl+Down` 按文档顺序跳到上一个/下一个标题行首；目标被折叠时自动展开必要的祖先标题。查找命中折叠正文时也会自动展开对应路径。折叠状态只属于当前编辑会话，载入新内容时重置，不写入用户配置或文件。
- 引用命令在空行生成 `> `，执行后不会保留自动选区。逻辑行第 0 列输入 `>` 会自动补为空格结尾的
  `> `；输入全角 `》` 会转换为 `> `。键盘输入和 IME 提交行为一致，转换与补空格可一次撤销；
  行中、缩进后、有选区或围栏代码块内保持字面输入。
- `Tab` 优先跳出括号、引号或 Markdown 强调标记；未触发跳出时，无论光标位于行内何处，都在行首增加 4 个空格。`Shift+Tab` 减少一级缩进。
- 括号、引号、行内代码与围栏代码支持自动补全，包含半角、全角及常用中文成对符号。
  光标位于行中时，引号类符号（`` ` ``、`"`、`'`、`“”`、`‘’` 等）只输入单个开符号，
  输入闭符号完成包裹后再收尾：包裹内容含中文时 ASCII 引号转为全角；全角引号包裹
  会在与相邻中文/字母数字之间补自动空格（如 `中文 “内容” 结束`），纯 ASCII 引号包裹保持原样；
  行尾的 CJK 引号自动补全同样补空格（如 `中文` 后输入 `"` 得到 `中文 “”`）；
  先输闭符号、再补开符号完成包裹时，边界自动空格行为与先开后闭一致。
- 行首（列 0）连续输入三个反引号自动补全围栏代码块：光标后本行仍有文字时，闭合围栏单独成行、
  后续文字移到其下一行（如 `|测试文字` 输入后得到 `` ``` ``、`` ``` ``、`测试文字` 三行）；
  光标后无文字时不额外加空行。行中、行末和带缩进的行首不触发围栏自动补全。
- 自动空格与触发它的输入动作合并为一次撤销（一次 `Ctrl+Z` 同时撤销输入与空格整理）。
- ASCII 片段后的右边界自动空格是临时的：光标停在空格前，若下一次输入以中文等 CJK
  字符结尾，该空格会随本次输入一并清除（如 `中文 abc` 后输入 `新的中文` 得到
  `中文 abc 新的中文`，而不是 `中文 abc 新的中文 中文`）；若后续仍是 ASCII 输入则保留。
- 中文标点自动转换：在 CJK 字符或全角 `，。：；？！）` 之后输入半角 `, . : ; ? ! )` 自动转为对应全角，`( [ " '` 生成全角配对；半角 `, . : ; ? !` 后连续输入两个空格同样转为全角并删除两个空格；仅直接键盘输入生效，IME 提交与粘贴不转换，行内代码/公式/围栏等保护区内不生效。Tab 跳出 `() [] " '` 半角配对时，若内容含 CJK 则整对转为全角 `（）【】“”‘’`（保护区同样不转换）。
- `·`（U+00B7 中间点）空格后输入时触发反引号转换：删除空格与 `·` 后生成反引号对
  `` `|` `` 并补自动空格；连续输入两个 `·`（紧贴字符）同样生成反引号对，光标居中并按
  边界规则补两侧自动空格；完全空行上生成反引号对后再输入 `·` 会升级为大代码块围栏；
  有选区时输入 `·` 等价于输入 `` ` ``，用反引号对包裹选区并触发自动空格；
  围栏代码块内不触发上述转换（一次 `Ctrl+Z` 撤销）。
- 空 Markdown 标记对与空围栏代码可用一次退格整体删除；`……`、`——` 也支持整体退格删除，空行输入 `-` 会自动补为 `- `。
- Enter 自动接续无序列表、有序列表和任务复选框（新任务重置为未勾选）；有序列表在增删换行、整行删除/剪切、选区替换、粘贴、拖动和缩进变化后会自动维护同层编号，连续列表以首项编号为起点，手工修改首项可更换起点。空列表项再次 Enter 会退出列表但保留空行，Backspace 则会整行删除并回到上一行末尾。列表续行与撤销会保持编辑点光标，不会跳回文档开头。
- 引用行有内容时，Enter 自动把完整引用头接续到下一行；只有引用头的空引用行按 Enter 会逐次退出最内层引用，直至保留普通空行。`Shift+Enter` 即使在空引用行也会保留全部层级并接续引用；引用内列表的普通 Enter 优先接续列表，`Shift+Enter` 只接续引用头。非引用列表中的 `Shift+Enter`、有选区、光标位于引用头内部及围栏代码块内仍为普通换行。
- 光标紧跟在引用头后时，一次 Backspace 删除完整的最内层 `> `；多层引用逐层删除，围栏代码块内保持普通退格。
- `Ctrl+L` 切换光标所在行的 checkbox 勾选状态；非 checkbox 行会转换为未勾选任务，并保留已有列表编号、缩进和多层引用前缀。
- 光标已经位于文档最后一个可视行时，Down 会转到行尾；位于第一个可视行时，Up 会转到行首。
- `PageUp`/`PageDown` 按一页纯滚动浏览，不移动光标、不改变选区；`Shift+PageUp`/`Shift+PageDown` 同样只滚动。文档末尾下方保留 2/3 页可滚动的空白区（短文档不足一页时不产生滚动）。键盘输入、IME 提交、回车或粘贴使光标碰到/越过视口底边时，自动滚动一次：段中光标行滚到视口上 1/3；光标位于文档末尾时等效于滚到底（2/3 页留白翻出，光标停在上 1/3）。触发后继续输入不会再次触发，光标自然下落，再次触底才再次触发（间歇式）。退格、删除（含选区、结构删除与 `Ctrl+Backspace`/`Ctrl+Delete` 词删除）、剪切、命令面板的「删除整行」「剪切整行」「清空整个编辑区」，以及撤销（视作删除类编辑）使光标碰到/越过视口顶边时，按严格镜像规则自动滚动一次：光标行滚到视口距顶 2/3 处（下 1/3），光标位于文档开头时滚到顶部；继续删除再次触顶才再次触发。撤销与重做视为普通输入/删除共用同一套检查（不预设方向，编辑后光标落在哪条边就按哪条规则处理），不再与输入绑定回滚滚动位置。以上滚动在动画开关开启时使用约 160ms 的轻量平滑动画，关闭时瞬时到位。

## 界面与配置管理

全部 UI、动画与窗口显示参数（窗口默认/最小尺寸、布局边距与圆角、字号角色、
动画时长与延迟、各面板尺寸、深浅两套调色板、窗口锚定间距）集中保存在
`config/ui.json`。文件为 JSONC 格式，可写 `//` 与 `/* */` 注释；改动后重启
应用生效。稳定安装首次构建时把模板初始化到
`%LOCALAPPDATA%\ScratchEditor\ScratchEditor\ui.json`，后续构建不覆盖用户副本；
测试模式读取构建目录中的模板，也可用 `SCRATCHEDITOR_UI_CONFIG` 指定隔离配置。

配置模板集中保存在 `config/markdown-style.json`。其中 `theme.accentColor` 是界面强调色的单一事实
来源：设置页、命令面板、焦点边框、文本选区、拖动选区的落点光标和 Markdown 链接都使用该颜色；
`theme.accentTextColor` 控制强调色背景上的文字。行内代码和围栏代码的背景色、等宽字体及其他
Markdown 样式也由该文件管理。

普通文本行中的 `*` / `_` 强调按 CommonMark 的左右侧定界规则高亮：词内下划线保持字面量，
反斜杠可转义强调标记，粗体与斜体嵌套时组合显示。等长反引号 run 构成的行内代码和现有
`[标题](目标)` 链接优先于强调，内部标记不会与外部标记误配；标题、引用和任务等结构行继续
使用各自的整行样式。

稳定安装首次构建时会把模板初始化到
`%LOCALAPPDATA%\ScratchEditor\ScratchEditor\markdown-style.json`。Codex/pi 与 AHK 两个安装副本
共同读取并监听这一个用户配置；可随时手工保存修改，运行中的编辑器会自动热更新，无需重启。后续构建
不会覆盖该用户文件。测试模式继续读取对应构建目录中的模板，也可用
`SCRATCHEDITOR_MARKDOWN_STYLE` 指定隔离配置。

## 架构与目录

```text
KeysRedirect.ahk ──命名管道──> %LOCALAPPDATA%\ScratchEditor\AhkEditor\ScratchEditor.exe
Codex / pi ───────文件模式───> %LOCALAPPDATA%\ScratchEditor\CodexEditor\ScratchEditor.exe
共享 Markdown 主题配置 ──────> %LOCALAPPDATA%\ScratchEditor\ScratchEditor\markdown-style.json
共享 UI 设计令牌配置 ────────> %LOCALAPPDATA%\ScratchEditor\ScratchEditor\ui.json
                                             ├─ C++：应用协调、IPC、配置、编辑命令、窗口过渡
                                             │       └─ ClipboardHistoryCoordinator：剪贴板访问、历史领域与持久化
                                             └─ QML：编辑器、查找、命令面板、设置页与界面动效
```

- `src/`：C++20 应用与编辑核心。
- `qml/`：预编译 Qt Quick 界面；`Main.qml` 只协调窗口、编辑区与跨面板状态，
  查找替换、剪贴板历史、设置页和命令面板分别由独立组件实现。
- `integration/`：隔离的 AHK 迁移参考副本。
- `tests/`：C++ 验收程序和 AHK 测试夹具。
- `scripts/`：构建、功能回归和性能验收入口。
- `docs/`：历史归档、验收报告与功能分支文档。
- `artifacts/baselines/`：纳入版本控制的阶段最终证据。

完整架构和阶段门槛见 [ScratchEditor-Migration.md](docs/archive/ScratchEditor-Migration.md)，文档索引见
[docs/README.md](docs/README.md)。

## 后续维护路线

以下顺序用于约束后续重构范围；每一项均应独立实施和回归，不做跨层大规模重写：

1. **拆分 `EditorController` 职责**：剪贴板访问、历史加载/捕获/持久化和错误聚合已收敛到
   `ClipboardHistoryCoordinator`；下一步隔离测试 IPC 和性能基准设施。继续保持现有 QML 属性、
   IPC JSON、ready/test 门禁及窗口生命周期语义不变。
2. **按垂直能力治理 `EditorCommandRegistry`**：依次评估输入自动滚动、选区拖动和纯文本变换的
   提取，不做一次性重写；继续遵守 Qt UTF-16 索引、单次全文读取/分析及线性复杂度约束。
3. **拆分 editing 验收代码**：按编辑领域拆分 `tests/editing_main.cpp`，共享 IPC 客户端与断言工具，
   但保留现有测试可执行文件、脚本入口、独立 check 名和机器可读产物格式。

只有当前一项完成隔离构建与对应完整回归后，才开始下一项；纯粹移动代码时不得顺带改变产品行为。

## 工具链与构建

已验证工具链：Qt 6.10.2、MinGW 13.1、CMake 3.25+、Ninja、Windows 11。
工作区工具链默认位于 `.tools/Qt`，该目录不会提交到 Git。

首次配置或 `.tools` 缓存丢失后，运行以下脚本即可从 Qt 官方仓库恢复项目锁定的完整工具链：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\restore-toolchain.ps1
```

脚本使用临时 Python 3.13 环境和固定版本的 `aqtinstall`，先在 `.tools` 下暂存并校验
Qt 6.10.2、MinGW 13.1、CMake 与 Ninja，成功后再替换 `.tools/Qt`。完整环境重复执行会直接跳过；
非空但不完整的目录默认拒绝覆盖，确认其中没有需要保留的内容后可显式传入 `-Force`。

创建 worktree 时若把其中的 `.tools` 设为指向主工作区 `.tools` 的目录联接，必须使用受保护的入口移除：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\remove-worktree.ps1 `
  -WorktreePath <worktree>
```

脚本会确认路径属于已注册的非主 worktree；若存在 `.tools`，则要求其必须是 `Junction`，只解除联接并
确认共享工具链仍完整后才执行 `git worktree remove`。不要绕过脚本直接移除仍包含共享 `.tools`
联接的 worktree，也不要对该联接递归删除；Git for Windows 可能沿联接清空主工作区工具链。

```powershell
./scripts/build.ps1
```

在不覆盖 `build/release` 的情况下进行隔离验证，可使用：

```powershell
./scripts/build.ps1 -Preset window-ui -SkipLocalInstall
```

隔离验证 preset 按职责命名：`editing` 覆盖 Markdown、高频编辑命令、查找替换与快捷键；
`window-ui` 覆盖设置、主题、窗口交互与动画。两组验证相互独立，完整回归时都应执行。

剪贴板历史功能可在不访问真实剪贴板、不部署稳定副本的前提下单独验证：

```powershell
./scripts/build.ps1 -Preset editing -SkipLocalInstall
./build/editing/ScratchEditorClipboardHistoryTests.exe
./scripts/run-clipboard-history-tests.ps1 -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.ClipboardHistory.Validation
./scripts/run-editing-tests.ps1 -BuildSubdirectory build\editing `
  -ServerName ScratchEditor.Editing.ClipboardHistory
./scripts/build.ps1 -Preset window-ui -SkipLocalInstall
./scripts/run-window-ui-tests.ps1 -BuildSubdirectory build\window-ui `
  -ServerName ScratchEditor.WindowUi.ClipboardHistory
./scripts/run-perf-tests.ps1 -BuildSubdirectory build\window-ui `
  -ServerName ScratchEditor.Perf.ClipboardHistory
```

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

### 在稳定版与临时测试版之间切换 AHK 快捷键

AHK 始终向固定生产管道发送 Scroll Lock、Win+F 等快捷键命令，因此无需修改或重载项目外的
`KeysRedirect.ahk`。仓库内切换器会先显示当前常驻实例，再正常保存并退出旧实例，最后让目标版本
接管同一管道：

```powershell
# 只查看当前状态，不切换
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 -StatusOnly

# 只列出当前工作区和所有已注册 worktree 中的可用构建
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 -ListCandidates

# 首次明确选择测试产物；相对路径按项目根目录解析
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 `
  -TestEditorPath .\build\editing\ScratchEditor.exe

# 之后无参数运行：测试版切回稳定版，稳定版切到交互选择的测试产物
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1
```

也可直接双击项目根目录的 `switch-ahk-editor.ps1.lnk` 执行同一切换流程。

当前无实例时，脚本会引导选择稳定版或测试版；切到测试版但未指定路径时，会扫描当前工作区以及
`git worktree list --porcelain` 返回的全部已注册 worktree，并按修改时间列出各自直接位于
`build/*/ScratchEditor.exe` 的候选。扫描不递归进入构建目录，并会跳过 worktree、`build` 或构建
子目录上的 reparse point。输出统一使用中文，其中 `[当前]`、`[目标]`、`[停止]`、`[启动]`、
`[已启用]`、`[警告]` 和 `[错误]` 等标志使用不同终端颜色，持续说明所处状态、PID 与完整路径。
目标会先完成存在性校验，启动后还会通过 IPC 复核 PID、可执行文件路径和 ready 状态；启动失败会
清理失败进程并尝试恢复刚才的版本。中文文案由 `config/switch-ahk-editor.zh-CN.json` 集中维护，
操作成功时会在结束提示后保留 2 秒缓冲，避免快捷方式窗口立即关闭；操作失败时窗口不会自动关闭，
只能由用户手动关闭，以便完整查看错误与恢复提示。
脚本显式以 UTF-8 读取，以兼容 Windows PowerShell 5.1 和项目 UTF-8 无 BOM 约束。

若当前常驻实例权限高于调用终端，脚本会显示 `access denied` 并保持现状；从管理员 PowerShell
重新运行同一命令即可。管道存在但无法识别实例时同样安全失败，不会误判为“当前无实例”。

切换使用现有 `shutdownForUpdate` 正常关闭语义：若窗口可见，当前草稿会先按普通关闭行为写回
剪贴板。正常退出超时后，脚本只会在重新核验旧实例 PID、启动时间和路径后询问是否强制终止；
非交互环境不会强制终止任何进程。测试版以普通常驻模式运行并共享正式设置、主题、剪贴板历史
和系统剪贴板，不使用 `--test-mode`，也不会覆盖 `%LOCALAPPDATA%\ScratchEditor\AhkEditor` 稳定副本。

切换器的自动验收使用唯一测试管道、内存剪贴板和临时 INI，不会连接或停止真实常驻实例：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-editor-switch-tests.ps1
```

用户可写配置存放在 Qt `AppConfigLocation` 下的 `settings.ini`（schema 版本 2）。
首次创建集中配置时会迁移旧 Native Settings 中的窗口几何和快捷键；从 schema 1 升级时
自动把 `editor/fontFamily`、`editor/fontPointSize`、`ui/animationsEnabled`
迁移到 `appearance/` 段落。测试通过独立环境变量 `SCRATCHEDITOR_SETTINGS_FILE`
使用临时 INI，不会触碰用户正式配置。

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
