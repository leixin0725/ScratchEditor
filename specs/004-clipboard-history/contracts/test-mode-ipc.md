# Contract: Clipboard History Test-Mode IPC

本契约中的扩展只允许 `--test-mode` 隔离实例使用，全部按现有 `Gate::Test` 注册。生产实例必须按现有 unsupported/unknown command 语义拒绝，并且不得返回历史正文、路径或故障状态。

## 1. Isolation Invariants

- test mode 的 listener lifecycle、sequence、clipboard read/write、change notification 和“发送到原目标”全部使用同一内存 `ClipboardGateway`。
- 任何本契约命令都不得调用 `AddClipboardFormatListener`、`RemoveClipboardFormatListener`、`GetClipboardSequenceNumber`、`OpenClipboard`、`SetClipboardData`、OLE clipboard 或真实键盘粘贴。
- `SCRATCHEDITOR_SETTINGS_FILE` 继续决定隔离 settings；历史文件从其父目录派生。runner 必须为每次运行创建唯一临时目录。
- 既有 `testSetClipboard` 语义保持：只设置虚拟当前值，不生成 clipboard-change，不进入历史。
- 测试结束只停止自己启动的 server/PID，并删除解析验证过的精确临时目录。

## 2. Status Extension

现有 `status` 在 test mode 可附加：

```json
{
  "historyAvailable": true,
  "historyHealthy": true,
  "historyError": "",
  "historyCount": 3,
  "historyStoreFile": "D:/.../clipboard-history.dat",
  "historyPanelOpen": true,
  "historyPanelOverlay": false,
  "historyPanelWidth": 306,
  "editorVisibleWidth": 578,
  "historyQueryFocused": true,
  "historySelectedId": "opaque-id",
  "historyLoadConfirmationVisible": false,
  "historyClearConfirmationVisible": false,
  "historyStoreState": "Ready",
  "clipboardBackend": "memory",
  "nativeClipboardAccessAttempts": 0
}
```

约束：

- `historyStoreFile` 只在 test mode 返回；测试必须断言它位于 runner 的临时目录。
- status 不返回任何历史全文或摘要。
- 数值几何允许 runner 使用小容差验证动画完成后的稳定值。
- external file mode 若提供 test status，必须返回 `historyAvailable=false` 且不返回可访问的 store 路径。
- `clipboardBackend` 在所有隔离验证中必须为 `memory`，`nativeClipboardAccessAttempts` 必须为 0；这两个字段替代任何对真实剪贴板内容的前后比较。

## 3. `testEmitClipboardChange`

显式驱动虚拟剪贴板候选，并等待 Controller 处理完成或返回确定性错误。

Request:

```json
{
  "command": "testEmitClipboardChange",
  "kind": "text",
  "text": "示例\ntext",
  "sequenceNumber": 42,
  "capturedAtMs": 1786200000000,
  "excludeFromHistory": false,
  "excludeFromMonitor": false
}
```

字段：

- `kind`: 必填，`text | empty | nonText | readFailure`。
- `text`: `kind=text` 时必填；其他 kind 忽略或拒绝非空值，但行为必须固定并测试。
- `sequenceNumber`: 可选非负 32-bit；缺失时由内存 gateway 生成下一个非零值。
- `capturedAtMs`: 可选 UTC Unix 毫秒；缺失时取当前测试进程时间。
- `excludeFromHistory`: 可选；true 模拟有效 DWORD 0。
- `excludeFromMonitor`: 可选；true 模拟 monitor exclusion format 存在，优先级更高。
- 为覆盖畸形 include format，可增加枚举值 `historyFormat="malformed"`；若采用该字段，必须保持向后兼容并记录在实现测试中。

Response:

```json
{
  "ok": true,
  "captured": true,
  "outcome": "inserted",
  "historyCount": 4,
  "visibleRevision": 9,
  "sequenceNumber": 42
}
```

`outcome` 必须是稳定值：`inserted`、`duplicateRefreshed`、`empty`、`nonText`、`readFailure`、`excludedFromHistory`、`excludedFromMonitor`、`oversize` 或 `selfWriteNotification`。前两者返回 `captured=true`；其余返回 `captured=false`。重复刷新虽然集合数量不变，仍必须完成可观察的 revision/时间更新。

## 4. `testClipboardHistoryState`

返回确定性的模型快照，仅限测试：

```json
{
  "command": "testClipboardHistoryState",
  "items": [
    {
      "id": "opaque-id",
      "text": "完整原文",
      "capturedAtMs": 1786200000000,
      "characterCount": 4
    }
  ],
  "visibleIds": ["opaque-id"],
  "query": "原文",
  "selectedId": "opaque-id",
  "revision": 9,
  "persistedRevision": 9
}
```

数组按模型全局新到旧排序；`visibleIds` 按当前筛选顺序。ID 视为不透明值，测试只验证非空、稳定和关系，不匹配 UUID 文本格式。

## 5. `testResetClipboardHistory`

清空内存模型、隔离历史文件、store 错误和故障注入，并等待 worker idle：

```json
{ "command": "testResetClipboardHistory" }
```

它不得改变虚拟当前剪贴板、编辑器文本、settings、窗口几何或 delivered text。只允许删除已验证位于隔离测试目录内的 history 文件和该 store 自己的临时文件；路径验证失败必须拒绝操作。

## 6. `testSetClipboardHistoryFault`

Request:

```json
{
  "command": "testSetClipboardHistoryFault",
  "operation": "write",
  "enabled": true
}
```

`operation` 必须是 `listenerRegistration | sequenceRace | read | decrypt | encrypt | write`。listener 和 sequence 故障在内存 gateway 相应边界确定性触发，其余故障在 read/store 边界触发；禁用后下一次适用操作允许恢复。测试结束或 `testResetClipboardHistory` 必须清除全部故障。

listener 生命周期恢复使用：

```json
{ "command": "testRestartClipboardMonitoring" }
```

该命令只停止并重新启动内存 gateway 的 monitoring lifecycle，用于验证注册失败错误和恢复；不得注册 Windows listener。

推荐补充 `testWaitForClipboardHistoryIdle`，等待指定 revision 已提交或进入失败态，避免 runner 依赖 sleep：

```json
{
  "command": "testWaitForClipboardHistoryIdle",
  "minimumRevision": 9,
  "timeoutMs": 5000
}
```

超时返回错误，不改变 store 状态。

## 7. UI Actions

UI 自动化必须调用与真实键鼠相同的 QML action 函数，不得在 IPC handler 中直接改业务状态。可通过一个 test-only dispatcher 暴露以下 action：

```text
historyHoverTriggerEnter
historyHoverTriggerLeave
historyPanelEnter
historyPanelLeave
historySetQuery(text)
historySelect(id)
historyActivateSelected
historyDoubleClick(id)
historyConfirmLoad
historyCancelLoad
historyDeleteSelected
historyRequestClear
historyConfirmClear
historyCancelClear
```

命令打开继续复用 `testExecuteCommand("clipboardHistory")`；键盘继续复用 `testKeyPress`；文本、几何、动画和虚拟剪贴板继续复用现有 test commands。dispatcher 返回 action 是否被 QML 接收，并等待下一帧/稳定状态后再回复，以便性能测试测到实际界面更新而非 IPC handler 时长。

## 8. Controlled Write Observability

现有或扩展的虚拟 clipboard 查询必须能断言：

- Esc/Ctrl+S 成功后虚拟当前文本等于编辑器文本；
- Ctrl+W 后虚拟当前文本不变；
- 发送路径只更新 `testDeliveredText`，不触发真实键盘或真实剪贴板；
- 成功受控写入只新增/刷新一次历史。测试随后用相同 sequence/text 注入通知，必须返回 `selfWriteNotification` 且 revision、时间和顺序不再变化。

## 9. Required Contract Tests

- 不创建窗口或 listener 的 dispatch 单元 fixture 以 `testMode=false` 调用全部新增 test commands，并断言均返回 unsupported；不得为此启动普通生产实例。
- external file mode 无历史 command/model/store 文件访问。
- `testSetClipboard` 不产生历史；`testEmitClipboardChange` 才产生。
- 排除优先级、空/非文本/读失败/超限不改变模型。
- 重启后密文恢复；原始文件字节不包含测试明文。
- read/decrypt 失败进入 `ReadLocked` 且不覆盖原文件；encrypt/write 失败保留 last-known-good。
- 920px push、500px overlay、宽度边界、动画关闭、hover 定时、焦点锁均由 status 可观察。
- dirty 取消完整保持编辑器和面板状态，确认则更新 baseline 并清 undo。

## 10. External File Test Snapshot

external file mode 不启动 resident IPC。仅当同时满足 `--test-mode --wait` 时，可接受环境变量 `SCRATCHEDITOR_EXTERNAL_TEST_STATUS_FILE`。路径必须由测试进程提供并在写入前解析确认位于 runner 的唯一临时目录；生产模式或越界路径必须忽略/拒绝。

QML root ready 后写入一次：

```json
{
  "historyAvailable": false,
  "historyStoreFile": "",
  "historyCommandRegistered": false,
  "historyPanelLoaded": false,
  "clipboardBackend": "memory",
  "nativeClipboardAccessAttempts": 0
}
```

`historyPanelLoaded=false` 表示历史 panel 的 QML `Loader` 未激活，而不仅是控件不可见。该文件是 test-only 临时证据，不新增生产 IPC/CLI。
