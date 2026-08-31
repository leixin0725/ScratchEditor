# 文档索引

当前使用方式和默认快捷键以根目录 [`README.md`](../README.md) 为准。历史文档统一
保存在 `archive/`，命名说明见 [`archive/NAMING-NOTE.md`](archive/NAMING-NOTE.md)。

## 更新记录

- `2026-08-31`：修复输入触发自动滚动后，撤销、退格或重做删除使正文高度收缩时编辑区短暂抽动的问题；文本变更前会预先保持视口，自动滚动判断继续使用不含缓冲的自然范围，并在锚定动画安全落位后释放保持。editing 与 window-ui 新增首帧、40ms 延迟检查及动画开启场景的视口连续性回归。
- `2026-08-30`：正文编辑区支持拖入一个或多个本地文件/目录，在当前光标或选区处插入
  绝对 Windows 路径；多项保持顺序并以单空格连接，含空白字符的路径自动加双引号，
  整批无效时原子拒绝且整次有效插入可单步撤销。常驻与 `--wait` 文件模式共用该行为。
- `2026-08-30`：修复中文输入法直接提交弯单双引号时的自动补全：行尾 `“` / `‘`
  现在会生成完整配对并应用边界自动空格，后续提交 `”` / `’` 会越过已有闭符号而不重复插入；
  对输入法状态错位产生的行尾孤立 `”` / `’`，也会在没有同类待闭合开引号时按开引号生成新配对。
- `2026-08-30`：剪贴板历史卡片支持拖入编辑区指定落点；复用选区拖动的落点提示与边缘滚动，
  拖动时按落点切换复制/禁止光标并显示窗口内半透明摘要；成功插入后选中新文本、关闭历史面板，
  并支持一次撤销完整恢复；实现边界与验收记录见
  [`archive/014-input-feature/`](archive/014-input-feature/)。
- `2026-08-26`：行尾输入引号会优先闭合当前行同类未闭合引号；编辑区新增 ASCII 主字体与
  中文 fallback 字体链，正文和 Markdown 代码区域默认统一使用 `Consolas → NSimSun`，
  设置页新增 100–900 九档字体粗细选择，默认常规（400），设置 schema 升至 4。
  行为与配置说明见根目录 [`README.md`](../README.md) 和
  [`config/README.md`](../config/README.md)。
- `2026-08-22`：`Ctrl+Up` / `Ctrl+Down` 标题跳转新增轻量滚动——每次成功跳转后尝试滚动，使标题首行居于编辑区上 1/3 分点，复用翻页/自动滚动约 160ms 平滑动画（关闭动画时瞬时到位）；跳转期间抑制瞬时贴边跟随，保证整段跳转为单一平滑移动，目标标题刚从折叠祖先展开时在布局落定后定位。
- `2026-08-22`：右上角状态面板快捷键提示新增“打开命令面板”“打开设置”两条，显示命令面板中配置的当前生效快捷键并随改键刷新；字数统计在原有“字”（UTF-16 字符数）之外新增“汉字”字符数统计（按码点计，不含假名、谚文与标点），见根目录 [`README.md`](../README.md) 的功能列表。
- `2026-08-22`：优化标题折叠与导航反馈：正文光标折叠后临时停在标题末尾，未主动移动时展开可恢复原位置；标题跳转增加先于文字绘制的半透明强调色高亮。
- `2026-08-22`：剪贴板历史面板改用主窗口共享布局约束：面板标题与“临时编辑器”原位对齐，
  搜索框顶部与编辑视口顶部对齐，历史列表继续保持搜索框和页脚的安全间距。
- `2026-08-22`：新增基于 Qt Quick Shapes 的可复用 Lucide 图标组件，标题折叠标记改用
  `chevron-down` / `chevron-right`，并保留原有状态配色、点击范围与布局语义。
- `2026-08-22`：修复标题折叠状态已变化但编辑区画面未实时刷新的问题；Qt Quick 文本尺寸和场景图节点现随 block 可见性同步失效，`window-ui` 新增不改变历史栏或编辑器宽度的下一帧像素回归检查。
- `2026-08-22`：新增 Markdown 标题层级折叠、常驻折叠标记与上一个/下一个标题导航，
  实现与验收边界见 [`archive/013-heading-folding-navigation/`](archive/013-heading-folding-navigation/)。
- `2026-08-21`：Markdown 引用块新增行首 `>` 自动补空格、全角 `》` 转换、普通/Shift Enter
  续行、多层空引用逐层退出及引用头一次退格删除；同时修复列表自动续行撤销后光标可能跳到文档开头的问题，见根目录
  [`README.md`](../README.md#编辑快捷键) 的“编辑快捷键”。
- `2026-08-13`：剪贴板历史面板新增专用内部圆角配置，左上、左下默认使用 8px 半径，
  使边缘轮廓清晰可见；右侧继续与正文编辑区直线拼接。
- `2026-08-12`：剪贴板历史面板的左上、左下外侧角复用统一大面板圆角，右侧继续与
  正文编辑区直线拼接；window-ui 新增闭合、打开及缩放后三种状态的圆角回归检查。
- `2026-08-11`：剪贴板访问、历史加载/捕获/持久化、监听稳定性重试、自写入回声抑制与
  monitor/store 错误聚合从 `EditorController` 收敛到 `ClipboardHistoryCoordinator`；
  QML 与 IPC 接口保持不变，边界和验证记录见
  [`archive/010-clipboard-history-coordinator/`](archive/010-clipboard-history-coordinator/)。
- `2026-08-11`：QML 主窗口按功能边界拆分——`Main.qml` 保留窗口、编辑区和跨面板协调，
  查找替换、剪贴板历史、设置页与命令面板迁入独立组件；后续 `EditorController`、
  `EditorCommandRegistry` 与 editing 验收代码的重构顺序和边界见根目录
  [`README.md`](../README.md#后续维护路线)。
  - `2026-08-10`：修复窗口缩放与轻量关闭动画期间右侧滚动条短暂闪现——滚动条可见性
    由 60ms 防抖快照改为实时跟随 `editorViewport.contentHeight`（滑块尺寸仍按 60ms
    合并更新），window-ui 测试新增 `resizeScrollbarStaysHidden` 与
    `closingAnimationScrollbarStaysHidden` 回归检查。
  - `2026-08-10`：修复轻量动画开启时窗口缩放导致编辑区（缓冲区）边缘“迟钝→加速”追赶的问题——
    编辑区几何改为直接绑定布局进度（`historyLayoutProgress`），缩放期间即时跟随窗口边缘，
    历史面板开/合仍保留滑动动画；window-ui 测试新增
    `editorResizeFollowsInstantlyOpen`/`editorResizeFollowsInstantlyClosed` 回归检查。
  - `2026-08-10`：修复唤出窗口时历史面板右边缘在左边框与编辑区域交界处闪现的问题——
    历史面板闭合滑动改为只在开/合状态切换时动画，窗口缩放期间右缘即时跟随、不外露；
    window-ui 测试新增闭合态缩放回归检查（`historyClosedResizeKeepsEdgeClipped`）。
  - `2026-08-10`：编辑器新增 `PageUp`/`PageDown` 按页纯滚动浏览、正文下方 2/3 页可滚动留白，以及输入触底/删除触顶的间歇式自动滚动；删除、剪切与命令面板删除类动作一并纳入，撤销/重做视为普通编辑共用同一套检查；上述翻页与自动滚动在动画开关开启时使用约 160ms 轻量平滑滚动，关闭时瞬时到位，见根目录 [`README.md`](../README.md#编辑快捷键) 的“编辑快捷键”。
  - `2026-08-10`：配置文件重构——新增 JSONC 格式的 `config/ui.json` 统一管理 UI/动画/窗口显示
    设计令牌（窗口尺寸、布局、字号角色、动画时长、调色板、面板参数），`settings.ini` schema
    升至 2 并把外观键迁移到 `appearance/` 段落，见根目录 [`README.md`](../README.md#界面与配置管理)
    与 [`config/README.md`](../config/README.md)。
- `2026-08-10`：命令面板新增「清空整个编辑区」（默认 `Alt+X`，可单次撤销），见根目录
  [`README.md`](../README.md#编辑快捷键) 的“编辑快捷键”。
- `2026-08-10`：编辑器词边界中文适配（`Ctrl+左/右` 与双击按词选择使用中英文
  自适应边界），持续维护细节见根目录 [`README.md`](../README.md#编辑快捷键)
  的“编辑快捷键”。
- `2026-08-09`：剪贴板历史的当前功能、隐私边界与隔离验证入口见根目录
  [`README.md`](../README.md#剪贴板历史)；持续维护细节只保留在该正式文档。
- [`2026-08-08-editor-core-refactor/`](2026-08-08-editor-core-refactor/README.md)：编辑命令核心复用重构记录（行范围/成对包裹/命令分发/行变换拆分、IME 引号收尾统一与后续建议）。
- [`2026-08-06-status-panel/`](2026-08-06-status-panel/README.md)：右上角状态块拓展（字数统计、悬停状态面板、错误信息复制与状态面板配置）。
- [`2026-08-05-window-hotplug/`](2026-08-05-window-hotplug/README.md)：窗口放置与
  外部提示词编辑器更新（独立几何记忆、多屏热插拔修复、外部文件静默退出、CLI 类型标题）。
- `2026-08-06`：WSL 外部提示词编辑器部署适配，见根目录 [`README.md`](../README.md#cli-外部编辑器)
  的「CLI 外部编辑器」与 [`scripts/configure-codex-editor-wsl.sh`](../scripts/configure-codex-editor-wsl.sh)。
- `2026-08-06`：编辑器整行处理（三击选整行、无选区整行复制/剪切/智能粘贴），见根目录
  [`README.md`](../README.md#编辑快捷键) 的“编辑快捷键”。

## 历史归档

归档报告是对应验收时点的历史快照，已记录的命令数量、快捷键、进程 ID、哈希和性能
数值不随后续功能改动重写。归档内的 `stageN` 为 2026-08-04 重构前的旧命名，见
[`archive/NAMING-NOTE.md`](archive/NAMING-NOTE.md)。

- [`archive/ScratchEditor-Migration.md`](archive/ScratchEditor-Migration.md)：权威架构、约束和阶段路线。
- [`archive/STAGE1-PLAN.md`](archive/STAGE1-PLAN.md)：性能原型计划与验收方法。
- [`archive/STAGE1-REPORT.md`](archive/STAGE1-REPORT.md)：性能与输入原型报告。
- [`archive/STAGE2-REPORT.md`](archive/STAGE2-REPORT.md)：功能对等与 AHK 回退报告。
- [Markdown 编辑增强历史报告](archive/STAGE3-REPORT.md)（按用户决定不含预览）。
- [窗口界面历史报告](archive/STAGE4-REPORT.md)：设置页、外观、集中配置，以及窗口交互与动画稳定性回归。
- [`archive/STAGE5-REPORT.md`](archive/STAGE5-REPORT.md)：项目收尾、最终回归与 Git 初始化报告。
- [`archive/STAGE6-REPORT.md`](archive/STAGE6-REPORT.md)：原 AHK 备份、旧 GUI 移除、IPC/回退与最终验收报告。
- [`archive/cjk-features/`](archive/cjk-features/)：CJK 输入优化需求归档（已完结）——
  [最终验收清单](archive/cjk-features/cjk-features-final.md)、
  [修复计划](archive/cjk-features/cjk-features-fix-plan.md)、
  [原始实施计划](archive/cjk-features/cjk-features-plan.md)。
- [`archive/001-external-editor/investigation-report.md`](archive/001-external-editor/investigation-report.md)：Codex、pi-coding-agent 和 Claude Code 外部编辑器兼容性调查。
- [`archive/001-external-editor/extension-plan.md`](archive/001-external-editor/extension-plan.md)：外部编辑器瞬态文件模式的扩展计划与验收标准。
- [`archive/010-clipboard-history-coordinator/`](archive/010-clipboard-history-coordinator/)：
  剪贴板历史协调器的职责边界、兼容约束与验证记录。
- [`archive/013-heading-folding-navigation/`](archive/013-heading-folding-navigation/)：
  Markdown 标题折叠、Lucide 常驻图标、标题导航、隐藏命中自动展开与历史面板布局对齐的实现和验收记录。
- [`archive/014-input-feature/`](archive/014-input-feature/)：
  剪贴板历史卡片拖入编辑区的交互边界、UTF-16 落点、撤销语义与验收记录。

历史验收的机器可读证据统一保存在 [`../artifacts/baselines/`](../artifacts/baselines/)。
中间调优结果在本地 `artifacts/archive/` 中保留，但不进入版本控制。
