# 临时编辑器迁移调查与技术建议

更新日期：2026-08-01

## 1. 文档目的

本文记录 `KeysRedirect.ahk` 中临时编辑器功能的现状、后续需求、候选框架调查结果，以及推荐的迁移架构和实施路线。

此次迁移不要求替换整个 AutoHotkey 脚本。AHK 仍适合负责全局快捷键、窗口调度和其他自动化功能；迁移范围仅限已经逐渐复杂化的临时编辑器 UI。

## 2. 当前实现与迁移动机

当前临时编辑器由 AutoHotkey v2 `Gui` 和原生 Win32 `Edit` 控件实现，已经包含：

- 剪贴板内容载入与关闭时回写。
- 置顶、无标题栏窗口。
- 自动换行。
- 四边和四角缩放。
- 深色界面和中文 UI 字体。
- 按内容智能显示滚动条。
- 窗口复用和首帧闪烁规避逻辑。

为了实现这些效果，现有代码已经开始直接处理：

- Win32 窗口命中测试和原生缩放消息。
- DWM 圆角、暗色模式和边框属性。
- 原生滚动条主题与显示状态。
- 字体度量、编辑区高度计算。
- 透明窗口、离屏显示和首帧合成。

这些逻辑说明 AHK 仍能完成任务，但继续扩展 UI 会产生较高的维护成本。后续若加入 Markdown、命令面板、复杂编辑动作和动画，AHK 不再是合适的主要 UI 框架。

## 3. 需求优先级

当前需求按优先级排列如下：

1. 流畅的输入手感、轻量动画，以及未来继续扩展 UI 的能力。
2. 极快的启动和唤出速度、低空闲开销及较高运行效率。
3. 更进阶的编辑器能力，包括复杂快捷键、Markdown 渲染和快捷编辑命令。

补充约束：

- 主要运行平台为 Windows 11。
- CJK 字体、中文输入法和高 DPI 显示必须可靠。
- 临时编辑器应保持轻量，不应发展成完整 IDE 后才开始优化。
- 唤出延迟比安装包体积更重要。

## 4. 候选框架对比

| 方案 | UI 与动画 | 启动和运行效率 | 编辑器能力 | 主要问题 | 结论 |
|---|---|---|---|---|---|
| Qt Quick/QML + C++ | GPU 场景图、动画能力强、UI 易扩展 | 原生编译，无浏览器或托管运行时 | `QTextDocument`、Markdown、语法高亮、输入法支持 | 不是完全原生的 WinUI 外观；高级编辑命令需要自行组织 | 首选 |
| Tauri 2 + CodeMirror 6 | Web UI 灵活，CSS 动画和组件生态丰富 | 比 Electron 轻，但仍使用 WebView2 多进程 | 快捷键、多选、搜索、语言扩展和 Markdown 生态非常强 | 冷启动和常驻内存无法做到原生最低水平 | 功能生态优先时的备选 |
| WinUI 3 + C++ | 最接近 Windows 11 原生界面，Composition 动画优秀 | 原生代码性能好，但有 Windows App SDK 初始化和部署成本 | 基础文本控件可以使用，高级编辑器生态偏弱 | 若嵌入 WebView2 编辑器，会重新引入 WebView 开销和架构复杂度 | 原生外观优先时的备选 |
| WPF + C# + AvalonEdit | 框架成熟，样式和动画易实现 | .NET 冷启动及托管运行时开销高于纯 C++ | 编辑器组件成熟，Markdown 可通过第三方库实现 | 不符合“极致启动与效率”的最高优先级 | 不作为首选 |
| Electron + Monaco/CodeMirror | UI 和编辑器生态最成熟 | 自带 Chromium 与 Node，进程和内存开销明显 | 很强 | 与轻量、极快目标冲突 | 排除 |

## 5. 最终推荐

推荐目标框架：**Qt 6 Quick/QML + C++ 后端**。

建议技术组合：

- UI：Qt Quick、QML、Qt Quick Controls。
- 核心语言：C++20。
- 构建系统：CMake。
- 编辑文档：`QQuickTextDocument` / `QTextDocument`。
- Markdown 源码编辑：纯文本 `TextArea` 或 `TextEdit`。
- 语法着色：C++ `QSyntaxHighlighter`。
- 命令系统：C++ Command Registry，向 QML 暴露 `Action`。
- 单实例与进程通信：`QLocalServer` / `QLocalSocket`。
- 设置持久化：`QSettings` 或轻量 JSON。

Qt Quick 的 Scene Graph 面向动态 UI 和动画。官方性能文档建议预编译 QML，并避免在逐帧动画中运行复杂 JavaScript：

- [Qt Quick 性能指南](https://doc.qt.io/qt-6/qtquick-performance.html)

Qt 的文本系统已经提供 Markdown 和语法着色基础能力：

- [Qt Quick TextEdit](https://doc.qt.io/qt-6/qml-qtquick-textedit.html)
- [QTextEdit Markdown 支持](https://doc.qt.io/qt-6/qtextedit.html)
- [QSyntaxHighlighter](https://doc.qt.io/qt-6/qsyntaxhighlighter.html)
- [Qt Quick Controls Text Editor 示例](https://doc.qt.io/qt-6/qtquickcontrols-texteditor-example.html)

### 5.1 为什么不直接选择 Tauri

Tauri 2 是最有竞争力的备选方案。它可以直接使用 CodeMirror 6，后者已有可组合快捷键、撤销、多选、搜索、折叠、自动补全和语言扩展：

- [CodeMirror 6 Reference](https://codemirror.net/docs/ref/)

但 Tauri 在 Windows 上使用 Edge WebView2，并采用核心进程加 WebView 进程的多进程模型。它不打包完整 Chromium，因此显著轻于 Electron，但不能等同于单进程原生 C++ 程序：

- [Tauri Process Model](https://v2.tauri.app/concept/process-model/)
- [Tauri Windows prerequisites](https://v2.tauri.app/start/prerequisites/)

当编辑器需求发展到多光标、插件、LSP、复杂代码编辑或 VS Code 级命令体系时，Tauri + CodeMirror 6 的价值会明显上升。在当前需求范围内，Qt 的原生文本能力足够，并且更符合启动和效率优先级。

### 5.2 Markdown 编辑方式

不建议直接把 Markdown WYSIWYG 视图作为主要输入控件。按最终产品决定，编辑器只提供
Markdown 源码编辑与原生语法高亮，不提供预览：

```text
Markdown 纯文本源
       │
       └── 编辑区：PlainText + QSyntaxHighlighter
```

这样可以保证：

- 原始 Markdown 不会因富文本转换而发生不可控变化。
- 快捷编辑命令可以直接处理选区和文本范围。
- 不引入额外渲染视图、浏览器内核或预览同步状态。
- 首次唤出只需准备源码编辑界面。

## 6. 推荐架构

```text
┌─────────────────────────────────────────┐
│ KeysRedirect.ahk                        │
│ 全局快捷键、其他按键映射、启动与唤出     │
└───────────────────┬─────────────────────┘
                    │ 本地 IPC：toggle/show/hide
                    ▼
┌─────────────────────────────────────────┐
│ ScratchEditor.exe                       │
│ Qt/C++ 单实例常驻进程                    │
├─────────────────────────────────────────┤
│ Application Core                        │
│ 生命周期、剪贴板、设置、窗口状态/过渡、IPC │
├─────────────────────────────────────────┤
│ Editor Core                             │
│ 文档、撤销栈、选区、命令、Markdown 操作  │
├─────────────────────────────────────────┤
│ QML UI                                  │
│ 编辑器、标题栏、界面动效、命令面板、设置页 │
└─────────────────────────────────────────┘
```

### 6.1 唤出流程

为获得接近即时的唤出速度，不应在每次按快捷键时完整冷启动 UI：

1. AHK 启动时，以后台模式启动 `ScratchEditor.exe --background`。
2. Qt 程序创建单实例进程，初始化最小 UI 后隐藏窗口。
3. 用户触发快捷键时，AHK 向本地 IPC 发送 `toggle`。
4. Qt 程序读取剪贴板并显示已经存在的窗口。
5. 关闭时将内容回写剪贴板并隐藏，不销毁窗口或进程。

这样“快捷键到可输入”的延迟主要是窗口显示和焦点切换时间，而不是进程、Qt 和 QML 的冷启动时间。

### 6.2 编辑命令系统

不要把所有快捷键直接写在 QML 事件回调中。推荐建立统一命令层，例如：

```text
toggleBold
toggleItalic
cycleHeading
toggleTask
continueList
indentSelection
outdentSelection
wrapCode
moveLineUp
moveLineDown
duplicateSelection
openCommandPalette
```

每个命令应包含：

- 稳定的命令 ID。
- 默认快捷键。
- 是否能在当前选区执行的状态判断。
- C++ 文本变换实现。
- 可选的菜单、工具栏和命令面板描述。

这种结构便于未来支持快捷键自定义，也能避免 UI 与编辑逻辑耦合。

## 7. 性能策略

### 7.1 必须遵守

- 不使用 Qt WebEngine 渲染 Markdown。
- 使用 Qt 自带 Markdown 解析和文本布局。
- 预编译 QML。
- 启动时只创建标题栏和基础编辑器。
- 设置页和命令面板延迟创建。
- QML 界面动效使用 `Animator`、`Behavior` 和 `Transition`；窗口级透明度与几何过渡使用 Qt C++ 动画类，均避免逐帧 JavaScript。
- 空闲时不运行高频定时器。
- Markdown 高亮按文本块增量更新，避免每次按键同步重建整个文档格式。
- 窗口关闭时隐藏并复用，不重复构造字体、文档和渲染树。

### 7.2 原型验收目标

以下数值是迁移原型的初步门槛，不是未经测量的性能承诺：

| 指标 | 初步目标 |
|---|---|
| 常驻后的热唤出延迟 P95 | 不高于 50 ms |
| 冷启动至可输入 | 不高于 300 ms |
| 普通输入到画面更新 | 不超过一个 60 Hz 帧周期 |
| 空闲 CPU | 接近 0%，不得由轮询定时器持续唤醒 |
| 空闲工作集 | 尽量控制在 80 MB 以内 |
| 轻量动画 | 稳定 60 FPS，无逐帧主线程脚本 |
| 10 万字符纯文本编辑 | 输入、选择和滚动无明显阻塞 |

最终门槛应在实际目标电脑上测量。若 Qt 原型无法达到热唤出、输入延迟或内存目标，应先分析实现问题，再决定是否降低 UI 复杂度。

## 8. 分阶段迁移路线

当前阶段 1–6 均已完成。以下内容保留各阶段的范围定义；实际验收结果见
[`docs/README.md`](docs/README.md)。

### 阶段 0：冻结 AHK UI 功能范围

- 保留现有 AHK 编辑器作为可用版本。
- 迁移期间只修复阻断性问题，不继续增加大型 UI 功能。
- 明确现有行为作为兼容性基线。

### 阶段 1：性能与输入原型

只实现：

- Qt 单窗口启动、隐藏和唤出。
- AHK 到 Qt 的 `toggle` IPC。
- 剪贴板载入与回写。
- 中文输入法、CJK 字体和高 DPI。
- 无白闪显示、置顶和窗口缩放。
- 纯文本输入和自动换行。

完成后测量第 7.2 节的性能门槛。原型不达标时，不进入 UI 美化阶段。

### 阶段 2：功能对等

- 深色主题。
- 位置和窗口大小记忆。
- 智能滚动条。
- Escape 关闭并复制。
- 焦点恢复和剪贴板异常处理。
- AHK 版本与 Qt 版本之间的回退开关。

### 阶段 3：Markdown 与快捷编辑

- Markdown 语法着色。
- 加粗、斜体、标题、列表、任务、引用和代码块命令。
- 搜索替换。
- 命令面板。
- 明确不提供 Markdown 预览。
- 可配置快捷键。

### 阶段 4：扩展 UI，完善配置管理

- 轻量的设置页面，不重点拓展。
- 主题和字体配置。
- 轻量过渡动画。
- 完善配置文件的管理，集中管理所有配置文件。
- 暂不实现历史草稿、标签页或固定草稿。
- 暂不实现多光标、插件和 LSP。

### 阶段 5：收尾

- 清理项目目录，集中留存测试文件，工作文档等，更新项目文档，初始化git仓库并提交。

### 阶段 6：移除 AHK 编辑器模块（已完成）

经用户明确批准并先创建同目录备份后：

- 从 `KeysRedirect.ahk` 删除旧 GUI 实现。
- 保留快捷键和 IPC 调用。
- 保留一个启动失败时的纯剪贴板回退路径。

## 9. 风险与决策检查点

### Qt 方案风险

- Qt Quick 外观需要自行设计，无法自动获得完全一致的 WinUI 3 控件表现。
- 多光标、LSP 和完整插件系统不是 Qt 文本控件的开箱功能。
- 大文档下的 Markdown 高亮必须按文本块增量处理。
- 发布前需要确认所选 Qt 版本、许可和链接方式满足分发要求。

### 何时改选 Tauri + CodeMirror 6

如果以下需求成为核心，应重新评估 Tauri：

- 多光标和矩形选择。
- 大量编辑器插件。
- LSP、诊断、自动完成和代码导航。
- 与 Web Markdown 生态高度一致的渲染。
- UI 迭代速度高于常驻内存和冷启动效率。

### 何时改选 WinUI 3

如果“完全原生的 Windows 11 视觉与无障碍行为”超过编辑器生态和部署简洁性，应重新评估 WinUI 3：

- [WinUI 3 overview](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/)
- [Windows Composition visual layer](https://learn.microsoft.com/en-us/windows/apps/develop/composition/visual-layer)

## 10. 最终决策摘要

当前决策为：

> 使用 Qt 6 Quick/QML + C++ 构建独立的 `ScratchEditor.exe`；AHK 继续负责全局快捷键并通过本地 IPC 控制编辑器。编辑器采用纯 Markdown 源码模式和原生语法高亮，不提供 Markdown 预览，也不引入浏览器内核。

这个方案并非拥有最完整的现成编辑器生态，但在当前三个优先级之间取得了最合适的平衡：

1. Qt Quick 提供流畅动画和长期 UI 扩展能力。
2. C++ 原生进程和常驻窗口满足快速唤出与运行效率目标。
3. Qt 文本系统足以支持当前规划的 Markdown、快捷键和快捷编辑功能。

阶段 1 的垂直原型及后续全量回归已用实测数据验证启动、内存、输入和动画表现；
最终状态与证据索引见 [`docs/README.md`](docs/README.md)。
