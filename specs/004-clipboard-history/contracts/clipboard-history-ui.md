# Contract: Clipboard History UI and Controller

本契约描述生产应用内部的 C++/QML 边界。它不是新的进程间 API，不增加命令行参数或 AHK 映射。

## 1. Availability and Lifecycle

- 历史能力只在普通常驻剪贴板模式可用。
- external file mode 必须满足：不创建历史 model/store，不读取或写入历史文件，不注册 Windows 剪贴板 listener，不注册历史 UI 命令，QML 不显示触发条或面板。
- test mode 使用内存 gateway，不注册系统 listener；是否显示历史能力由其是否为普通常驻模式决定。
- 窗口 HWND 创建后注册 listener，注册时只记录序列基线；不得导入注册前已有的剪贴板正文。
- 退出时先通过 gateway 移除 listener，再最多等待 store 最新 revision 10 秒；成功后退出，失败或超时则丢弃未 commit 的临时文件、记录诊断并保留 last-known-good。

## 2. Command Registry

新增一条普通模式专用 UI command：

| 字段 | 值 |
|---|---|
| `id` | `clipboardHistory` |
| `title` | `打开剪贴板历史` |
| `category` | `界面` |
| `uiCommand` | `true` |
| `defaultShortcut` | 空 |

命令进入现有命令面板并使用现有通用快捷键配置持久化。执行后打开面板、聚焦搜索框并保持展开；不引入默认全局或窗口快捷键。external file mode 查询或执行该 ID 时按现有 unknown/unsupported command 语义失败。

## 3. Controller Surface

命名可根据现有 Qt 风格做纯机械调整，但能力和语义必须保持。

### Properties

```text
QAbstractItemModel* clipboardHistoryModel       CONSTANT
bool clipboardHistoryAvailable                 CONSTANT
bool clipboardHistoryHealthy                   NOTIFY
QString clipboardHistoryError                  NOTIFY
```

`clipboardHistoryModel` 的 QML roles：

| role | 类型 | 说明 |
|---|---|---|
| `historyId` | string | 稳定不透明 ID |
| `previewText` | string | 列表摘要，不是载入依据 |
| `capturedAtMs` | integer | UTC Unix 毫秒，QML 本地化显示 |
| `characterCount` | integer | Qt UTF-16 code unit 数 |

不得暴露全文 role；载入必须通过 ID 请求 Controller。

### Invokable Methods

```text
setClipboardHistoryFilter(QString query)
requestLoadClipboardHistory(QString id)
confirmLoadClipboardHistory()
cancelLoadClipboardHistory()
deleteClipboardHistoryItem(QString id)
requestClearClipboardHistory()
confirmClearClipboardHistory()
cancelClearClipboardHistory()
```

### Signals / QML State Changes

- 请求载入 dirty 缓冲区时，Controller 发出或设置 `historyLoadConfirmationVisible`；确认前不改编辑器或历史。
- 载入成功后通知 QML 折叠面板、清空 query、聚焦编辑器。
- 请求清空只显示确认；确认后清空内部历史，取消不产生任何业务变化。
- `clipboardHistoryError` 必须在面板中可见，并聚合到现有顶部错误区；普通剪贴板后续成功不得清除尚未恢复的 store 错误。

## 4. Capture and Ordering

- 只捕获 listener 启动后的合格纯文本变化。
- `ExcludeClipboardContentFromMonitorProcessing` 存在时跳过。
- `CanIncludeInClipboardHistory` 为有效 DWORD 0 时跳过；1 或缺失时允许；畸形时跳过并诊断。
- 空文本、非文本、读取失败、畸形 `CF_UNICODETEXT` 或 UTF-8 大小超过 1MiB 时不修改集合。
- 精确 `QString` 相等才去重；空白、换行、大小写、Unicode 码元差异都保留为不同条目。
- 重复文本保留稳定 ID，刷新时间并移动到最前；新文本生成新 ID；第 101 个唯一项加入后淘汰最旧项。
- 受控成功写入立即记录一次，随后只抑制一个匹配的 listener 回送通知。

## 5. Persistence and Privacy

- 文件位置为当前实例 `settings.ini` 同目录的 `clipboard-history.dat`。
- 整个有效负载必须由当前 Windows 用户范围的 DPAPI 加密；磁盘文件不得直接包含历史明文。
- whole-file 写入必须是原子替换；失败时保留 last-known-good 文件。
- 读取、解密、schema 或完整性失败时进入错误态，不自动以空集合覆盖原文件。
- 错误态下新捕获可在当前会话显示；自动保存保持锁止。用户明确确认清空时视为重置不可读存储的授权。
- 不进行网络同步，不将历史正文写入日志、settings.ini、崩溃 artifact 或 test status。

## 6. Panel Layout

- 折叠触发条宽 12px，位于编辑内容表面内侧；额外的透明 hover 感应区可覆盖窗口左框，但不得接受按键、改变光标或拦截窗口拖动/resize。
- 展开宽度：`clamp(window.width / 3, 200px, 360px)`。
- 如果扣除面板后编辑器可见宽度仍 `>= 320px`，面板从左向右展开并挤压编辑器；否则覆盖在编辑器之上，编辑器几何保持不变。
- overlay 面板层级高于编辑内容但低于确认模态层；不得把窗口最小宽度改大来规避 overlay。
- 使用现有 `animationsEnabled`。启用时沿用统一 transition duration；禁用时 duration 为 0。

## 7. Open, Focus, and Collapse

- hover 从窗口外侧或内侧进入左框感应区并持续 100ms 后打开；无鼠标按键时快速向左越出窗口有效纵向范围则立即打开；在感应区和面板之外持续 250ms 后折叠。
- hover 短暂经过不足 100ms 不应打开；离开后 250ms 内返回不应折叠。
- 命令打开进入 `OpenByCommand`，搜索框聚焦；搜索框或列表内的键鼠交互不触发 hover 收起。
- Escape、窗口隐藏或载入成功会折叠并清空 query。若确认层可见，Escape 先关闭确认层并恢复之前的面板状态。
- 搜索为大小写不敏感全文子串；列表仍按全局最新顺序展示命中项。

## 8. Selection and Loading

- 单击只选择，不载入。
- 双击所选项或列表/搜索导航状态下按 Enter 请求载入。
- 上下方向键在可见结果中移动；模型置顶或筛选重建后用稳定 ID 恢复选择。
- 若当前全文等于 baseline，或目标全文等于当前全文，可直接载入；否则显示“放弃当前修改并载入历史？”确认。
- 取消载入必须保持：编辑器全文、光标、选择、undo 状态、面板开合、query 和 selected ID。
- 成功载入必须：用完整原文替换编辑器、光标置末、清空 undo/redo、更新 baseline、折叠面板并聚焦编辑器。
- 历史项本身不可编辑。载入后修改并 Esc/Ctrl+S 成功提交会形成新项或按全文规则置顶；Ctrl+W 放弃时不写剪贴板、不新增历史。

## 9. Delete and Clear

- 删除只作用于所选历史 ID，不修改编辑器或系统剪贴板；删除不存在 ID 是无副作用失败/忽略。
- 清空必须二次确认；取消不改变集合、选择或 query。
- 确认清空只清内部历史和持久化快照，不清系统剪贴板和当前编辑缓冲区。
- 删除/清空触发持久化失败时，内存模型保持用户已看到的结果，并显示“本次会话已更新、磁盘仍为上一有效版本”的错误语义。
