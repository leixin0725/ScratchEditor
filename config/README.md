# 配置文件说明

本目录是 ScratchEditor 的配置模板源，运行时按“模板 → 用户副本”的约定初始化：
正式模式首次启动时，把模板复制到 Qt `AppConfigLocation`
（通常为 `%LOCALAPPDATA%\ScratchEditor\ScratchEditor\`）后读取用户副本；
后续构建不会覆盖用户已修改的文件。测试模式直接读取构建目录
`<可执行文件目录>/config/` 下的模板，并可用环境变量指定隔离副本。

## 文件分工

| 文件 | 职责 | 生效方式 |
|---|---|---|
| `ui.json` | 全部 UI/动画/窗口显示设计令牌：窗口尺寸、布局、字号角色、动画时长、面板参数、深浅调色板、窗口摆放、用户偏好默认值 | 重启生效 |
| `markdown-style.json` | Markdown 语法高亮格式与界面强调色（accentColor / accentTextColor） | 保存后自动热更新，无需重启 |
| `switch-ahk-editor.zh-CN.json` | 稳定版/测试版快捷键切换器的简体中文终端文案 | 下次运行切换脚本时生效 |
| `settings.ini`（运行时生成） | 用户可写设置：窗口几何、快捷键、主题/字体/动画开关、状态面板、剪贴板历史卡片高度 | 随应用写入，重启生效 |

强调色是界面强调色的唯一来源，`ui.json` 不重复定义，避免两处维护。

`switch-ahk-editor.zh-CN.json` 由 PowerShell 脚本显式按 UTF-8 读取，使脚本源码可以保持 ASCII
兼容，同时满足 Windows PowerShell 5.1 与项目 UTF-8 无 BOM 的共同要求。文案中的 `{0}`、`{1}`
等占位符由切换器按当前状态填充，增删或改名时必须同步更新脚本引用。

## ui.json

文件为 JSONC 格式（支持 `//` 与 `/* */` 注释），解析时自动剥离注释。
所有数值带默认值与合法范围校验：文件缺失、JSON 语法错误或字段越界时，
回退到内置默认值，并保证跨字段一致（如最小宽度不超过默认宽度）。
常用编辑入口：

- `window`：初始与最小尺寸（逻辑像素）。
- `layout`：边距、拖拽区、圆角、控件高度、间距、滚动条、编辑区内边距，
  以及标题折叠图标的 `headingFoldGutterWidth` / `headingFoldIconSize`。
- `fonts`：界面字体、等宽字体与字号角色；`editorDefaultSize`、
  `statusPanelDefaultSize` 同时是 `settings.ini` 对应设置的默认值来源。
- `animation`：过渡动画时长、窗口开合缩放比例、标题导航高亮的透明度/停留/淡出时长、
  历史面板悬停延迟、滚动指标刷新节流与性能探针参数。
- `panels`：状态文字、状态面板、查找面板、历史面板、确认对话框、
  命令面板与设置页的尺寸和布局参数。
- `palette`：dark / light 两套界面基础色角色。
- `placement`：窗口唤起时的锚定间距。
- `preferences`：主题与动画开关的默认值（可被设置页覆盖）。

测试或调试可用 `SCRATCHEDITOR_UI_CONFIG` 指定独立的 `ui.json` 路径。

## markdown-style.json

纯 JSON，不允许注释，键名即功能。保存用户副本后，运行中的编辑器监听并
热更新 Markdown 高亮与强调色；测试可用 `SCRATCHEDITOR_MARKDOWN_STYLE`
指定隔离配置。

## settings.ini

由 `QSettings` 写入的 INI 文件，写入时会重排整个文件，因此无法保留注释；
段落与键名均按功能命名。当前 schema 版本为 2，启动时自动把旧版
`editor/fontFamily`、`editor/fontPointSize`、`ui/animationsEnabled`
迁移到 `appearance/` 段落。主要段落：

- `[window]`：`geometry`（常驻窗口几何）、`externalGeometry`（外部编辑器尺寸记忆）。
- `[appearance]`：`theme`、`fontFamily`、`fontPointSize`、`animationsEnabled`。
- `[statusPanel]`：`fontSize`、`showDelayMs`、`hideDelayMs`、`maxWidth`。
- `[clipboardHistory]`：`cardHeight`（文件级配置，不进入设置页）。
- `[shortcuts]`：`<命令ID>` 自定义快捷键。
- `[meta]`：`schemaVersion` 等内部元数据，请勿手工修改。

测试使用独立环境变量 `SCRATCHEDITOR_SETTINGS_FILE` 指向临时 INI，
不会读写用户正式配置。
