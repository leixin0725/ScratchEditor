# Phase 1 Data Model: 剪贴板历史

## 1. ClipboardHistoryItem

历史中的一个不可变文本版本。集合操作可以更新时间和位置，但编辑器载入后的修改不会原地修改该对象。

| 字段 | 类型 | 规则 |
|---|---|---|
| `id` | 不透明 UUID 字符串 | 首次捕获时生成；精确重复置顶时保持；持久化后重启仍稳定 |
| `text` | `QString` | 非空纯文本；精确保留 Unicode、空白和换行；UTF-8 编码大小 `<= 1,048,576` 字节 |
| `capturedAtUtcMs` | `qint64` | UTC Unix 毫秒；重复捕获时刷新 |
| `characterCount` | `qsizetype`，派生 | `text.size()`，即 Qt UTF-16 code unit 数 |
| `previewText` | `QString`，派生 | 仅供列表显示；规范化为单个紧凑摘要但不改变 `text` |

验证失败的项目不得进入模型或快照。反序列化时还需拒绝空/重复 ID、重复全文、超过容量或大小上限、非法版本和长度。

## 2. ClipboardCaptureCandidate

来自生产 Win32 gateway 或测试内存 gateway 的一次候选事件。

| 字段 | 类型 | 说明 |
|---|---|---|
| `kind` | `Text / Empty / NonText / ReadFailure` | 只有 `Text` 可进入后续校验 |
| `text` | `QString` | `Text` 时存在 |
| `sequenceNumber` | `quint32` | 0 表示不可用，不参与等值抑制 |
| `capturedAtUtcMs` | `qint64` | 生产取当前 UTC；测试可注入确定值 |
| `excludeFromMonitor` | `bool` | 对应排除 monitor 格式；为 true 时优先跳过 |
| `includeInHistory` | `Allow / Deny / Missing / Malformed` | Deny 和 Malformed 跳过 |

资格顺序：读取成功 → 无 monitor 排除 → history 未拒绝且格式有效 → 非空文本 → UTF-8 大小合格 → 非一次性自身写入回送。

## 3. ClipboardHistoryCollection

`ClipboardHistoryModel` 的权威会话状态。

| 字段 | 类型 | 说明 |
|---|---|---|
| `items` | `QVector<ClipboardHistoryItem>` | 从新到旧，最多 100 条 |
| `visibleIndices` | `QVector<int>` | 当前查询命中的 `items` 索引，不复制全文 |
| `query` | `QString` | 大小写不敏感子串查询 |
| `selectedId` | 字符串/空 | UI 使用稳定 ID，不以列表行号表示选择 |
| `revision` | `quint64` | 每次持久化相关变化递增 |

### 不变量

- `items` 中 `text` 全局精确唯一；大小写、空白、换行或 Unicode 码元不同均视为不同。
- `items[0]` 是最近一次合格捕获或重复刷新。
- 超过 100 条时只淘汰尾部最旧项。
- 查询只改变 `visibleIndices`，不改变 `items` 顺序和持久化内容。
- 若 `selectedId` 仍存在且仍可见，筛选或置顶后保持选择；否则选择首个可见项或空。

### 操作

- `capture(candidate)`：忽略不合格候选；精确重复保留 ID、更新时间、移到首位；否则生成 ID 插入首位；截断容量；重建过滤索引；revision +1。
- `mergePersisted(items)`：加载期会话项优先；按磁盘顺序追加不存在的全文；截断容量；一次性提升 revision。
- `setFilter(query)`：重建 `visibleIndices`；不提升 revision。
- `deleteById(id)`：存在时删除并提升 revision；不修改剪贴板或编辑器。
- `clear()`：清空集合、查询相关选择并提升 revision；不修改剪贴板或编辑器。
- `textById(id)`：返回全文或 not-found，不把全文作为 QML delegate role。

## 4. PersistenceSnapshot

worker 消费的不可变集合快照。

| 字段 | 类型 | 说明 |
|---|---|---|
| `revision` | `quint64` | 用于丢弃过时完成通知和合并排队写入 |
| `items` | 有序项目数组 | 只保存 `id`、`text`、`capturedAtUtcMs`；派生字段不保存 |

### 明文 envelope

```text
magic | schemaVersion | payloadLength | binaryPayload | sha256(payload)

binaryPayload := revision | itemCount |
                 repeated(idByteLength | idUtf8 |
                          capturedAtUtcMs |
                          textByteLength | textUtf8)
```

所有整数采用固定字节序；`itemCount <= 100`，每段长度在分配前校验，`textByteLength <= 1,048,576`，并拒绝尾随字节。整个 envelope 作为 DPAPI 输入，磁盘文件只包含 DPAPI 密文。载入必须验证：DPAPI 成功、magic、支持的版本、精确长度、SHA-256、二进制 schema 和全部项目不变量。任何一步失败都不得把空状态自动写回原文件。

## 5. ClipboardHistoryStoreState

| 状态 | 含义 | 可捕获/显示 | 可自动保存 |
|---|---|---:|---:|
| `Loading` | 后台读取/解密历史；会话捕获先进入模型 | 是 | 否 |
| `Ready` | 已成功加载或确认文件不存在 | 是 | 是 |
| `WritePending` | 有最新 revision 正在或等待加密写入 | 是 | 是，合并到最新 revision |
| `WriteFailed` | 上次写入失败，磁盘仍为 last-known-good | 是 | 是，后续变化可重试 |
| `ReadLocked` | 文件读取、解密或校验失败 | 是 | 否，直到用户明确清空重置 |

### 状态转换

```text
Loading --成功/不存在--> Ready
Loading --读取/解密/校验失败--> ReadLocked
Ready/WriteFailed --集合变化--> WritePending
WritePending --最新 revision 提交成功--> Ready
WritePending --提交失败--> WriteFailed
ReadLocked --确认清空重置成功--> Ready
任意可写状态 --退出且 10 秒内完成--> flush 最新 revision 后结束
任意可写状态 --退出超时/失败--> 丢弃未提交临时文件并保留 last-known-good 后结束
```

`historyHealthy` 仅在 `Ready` 或无待报告错误的 `WritePending` 为 true。错误信息独立于普通剪贴板读写状态，不得被后续 clipboard success 清除。

## 6. SelfWriteSuppressionToken

| 字段 | 类型 | 规则 |
|---|---|---|
| `sequenceNumber` | `quint32` | 必须非零 |
| `textFingerprint` | 固定长度 hash | 与受控成功写入的精确文本对应 |
| `expiresAt` | monotonic deadline | 短时限，禁止无限屏蔽 |
| `unused` | `bool` | 只消费一次 |

只有到达通知的序列号和读取文本同时匹配且 token 未过期时才抑制；不匹配通知不会长期保留过期 token。

## 7. ClipboardHistoryPanelState

| 字段 | 类型 | 说明 |
|---|---|---|
| `mode` | `Collapsed / HoverPending / OpenByHover / OpenByCommand` | 命令打开时搜索框获得焦点并锁定展开 |
| `query` | `QString` | 折叠成功后清空；取消载入确认时保持 |
| `selectedId` | 字符串/空 | 跟随稳定模型 ID |
| `pendingLoadId` | 字符串/空 | 仅在载入确认期间设置 |
| `loadConfirmationVisible` | `bool` | dirty 且目标不同才显示 |
| `clearConfirmationVisible` | `bool` | 清空前必须显示 |
| `panelWidth` | 实数，派生 | `clamp(window.width / 3, 200, 360)` |
| `overlay` | `bool`，派生 | 挤压后编辑器宽度小于 320px 时为 true |
| `editorVisibleWidth` | 实数，派生 | overlay 时不变；push 时扣除 panelWidth，始终至少 320px |

### 关键转换

- 左框感应区 hover 100ms 或无按键快速向左越界：`Collapsed → OpenByHover`；离开面板和感应区 250ms：`OpenByHover → Collapsed`。
- 执行 UI 命令：任意非模态状态 → `OpenByCommand`，聚焦搜索框。
- 单击：只更新 `selectedId`；双击或 Enter：请求载入。
- dirty 请求载入：显示确认且冻结 pending ID；取消后恢复原面板状态；确认后载入、清 query、折叠并聚焦编辑器。
- Escape、窗口隐藏或成功载入：清 query 并折叠；清空确认层优先消费 Escape，只关闭确认。

## 8. Relations and Ownership

```text
EditorController
├── ClipboardGateway (生产或测试实现；拥有 listener/sequence/全部 clipboard API)
├── ClipboardHistoryModel (GUI 线程，权威会话集合)
├── ClipboardHistoryStore (worker，快照 I/O)
└── Editor baseline / pending load / suppression token

Main.qml
└── 只通过 Controller 属性、方法和 ClipboardHistoryModel 轻量 roles 交互
```

外部文件模式只保留原有文件会话和编辑器 baseline，不构造上述历史组件。
