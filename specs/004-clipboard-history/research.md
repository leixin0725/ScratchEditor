# Phase 0 Research: 剪贴板历史

本文件记录实现决策。API 结论仅采用 Microsoft 和 Qt 官方资料；产品行为以 [spec.md](spec.md) 为准。

## 1. Windows 剪贴板监听与生命周期

**Decision**: 在普通常驻模式中，复用 `EditorController` 已注册的 `QAbstractNativeEventFilter` 和现有 `QQuickWindow` HWND。Controller 只把 HWND 交给生产 `ClipboardGateway::startMonitoring`、接收 `WM_CLIPBOARDUPDATE` 并调度读取；gateway 内部调用 `AddClipboardFormatListener`、`RemoveClipboardFormatListener` 和 `GetClipboardSequenceNumber`。注册时记录当前 sequence 作为基线，不导入启动前已有内容。test mode 调用内存 gateway 的同名生命周期接口但不访问 OS；外部文件模式不启动任何 gateway 监听。

**Rationale**: 项目已有稳定的 HWND 和原生消息入口，不需要隐藏窗口或第二个消息泵。把 API 生命周期放进 gateway 可让内存实现确定性模拟注册失败和 sequence 竞态，也避免 Controller 绕过测试隔离。序列号只比较是否相等，不假定递增且不把 0 当成有效值；读取前后变化时重新排队最新内容。

**Alternatives rejected**:

- `QClipboard::dataChanged`: 无法在同一 Windows 读取契约中可靠处理历史排除格式、序列号和格式内容校验。
- message-only 隐藏窗口: 重复现有 HWND 生命周期和消息循环，增加清理风险。
- viewer chain API: 属于旧式链式协议，维护成本和故障面高于 format listener。

**Official sources**: [AddClipboardFormatListener](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-addclipboardformatlistener), [WM_CLIPBOARDUPDATE](https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-clipboardupdate), [RemoveClipboardFormatListener](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-removeclipboardformatlistener), [GetClipboardSequenceNumber](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber), [QAbstractNativeEventFilter](https://doc.qt.io/qt-6/qabstractnativeeventfilter.html)

## 2. 捕获资格、排除格式与文本读取

**Decision**: 生产 `ClipboardGateway::readHistoryCandidate` 在一次成功的 `OpenClipboard` 会话内完成资格判断：先检查 `ExcludeClipboardContentFromMonitorProcessing`，再读取 `CanIncludeInClipboardHistory`，最后检查并读取 `CF_UNICODETEXT`。排除格式存在即跳过；`CanIncludeInClipboardHistory` 为 DWORD 0 时跳过、1 或缺失时允许、内容畸形时 fail closed 并报告诊断。空文本、非文本、无法读取、无 NUL 终止、奇数字节或 UTF-8 大小超过 1MiB 均不改变历史。

`GlobalSize` 只用于限定可访问缓冲区。gateway 把锁定后的字节范围交给无 OS 副作用的纯解码函数；该函数必须在范围内找到 UTF-16 NUL，并拒绝奇数字节、无终止符、越界和超限输入。复制到 `QString` 后再解锁和关闭剪贴板。`OpenClipboard` 竞争由 Controller 使用短间隔、有限次数的异步定时重试，不在 GUI 线程忙等。

**Rationale**: 排除资格和正文来自同一剪贴板状态可减少竞态；严格边界校验避免读取越界或把畸形格式当成有效记录。UTF-8 只用于存储上限，字符数仍按 Qt UTF-16 code unit。

**Alternatives rejected**:

- 分别打开剪贴板读取格式和正文: 两次之间可能发生内容变化。
- 把 `CanUploadToCloudClipboard` 当成本地历史开关: 该格式只控制云同步，不控制本机历史。
- 无限重试或同步 sleep: 会阻塞窗口消息处理。

**Official sources**: [Cloud clipboard and clipboard history formats](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats#cloud-clipboard-and-clipboard-history-formats), [RegisterClipboardFormatW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclipboardformatw), [OpenClipboard](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard), [CloseClipboard](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-closeclipboard), [Standard Clipboard Formats](https://learn.microsoft.com/en-us/windows/win32/dataxchg/standard-clipboard-formats), [GetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata), [IsClipboardFormatAvailable](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-isclipboardformatavailable), [GlobalLock](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globallock), [GlobalSize](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalsize)

## 3. 自身写入与重复通知

**Decision**: 普通模式成功写入剪贴板后，Controller 立即用同一文本更新历史，并记录写入后的非零序列号和文本指纹作为一次性 suppression token。随后仅跳过与该 token 同序列、同文本的一个 `WM_CLIPBOARDUPDATE`；token 使用短超时失效。普通 Copy/Cut 或外部应用写入没有 token，照常捕获。精确重复文本由模型刷新时间并移到首位，因此即使底层通知异常重复也不会生成第二项。

**Rationale**: 立即记录保证 Esc/Ctrl+S 的编辑结果可预测地进入历史，同时不把相应通知记录第二次。序列号可能不可用或回绕，故必须结合内容并采用一次性、有限时效规则。

**Alternatives rejected**:

- 忽略所有当前进程拥有的剪贴板: 会错误丢失应用内普通 Copy/Cut。
- 只依赖全文去重: 不会产生重复项，但会再次刷新时间并产生多余持久化和 UI 更新。
- 长时间按序列号屏蔽: 可能吞掉后续真实变化。

**Official source**: [QClipboard](https://doc.qt.io/qt-6/qclipboard.html)

## 4. 历史集合与 QML 暴露方式

**Decision**: 新增 `ClipboardHistoryModel : QAbstractListModel` 作为唯一集合业务模型。它持有最多 100 条完整记录以及筛选后的索引，只向 delegate 暴露稳定 ID、摘要、时间和 UTF-16 字符数；全文只能按 ID 载入编辑器。去重使用完整 `QString` 精确相等，重复项保留 ID、更新时间并移到首位；搜索使用大小写不敏感子串匹配。

**Rationale**: 最坏情况下全文约 100MiB。将全文放入 `QVariantList` 或 delegate role 会造成不必要的跨 C++/QML 复制。100 项的线性筛选可控，并与需求直接对应。

**Alternatives rejected**:

- `QVariantList` 全量刷新: 每次过滤或置顶可能复制大量正文。
- SQLite/全文索引: 100 项规模不需要数据库，且增加加密、部署和恢复复杂度。
- 把集合逻辑继续放入 `EditorController`: 扩大已有控制器职责并降低单元测试能力。

## 5. 加密、完整性与原子持久化

**Decision**: `ClipboardHistoryStore` 将有序项目序列化为确定性的长度前缀二进制 payload：revision、item count，以及每项的 ID UTF-8 字节、UTC 毫秒和正文 UTF-8 字节。所有整数使用固定字节序并逐字段设置上限。payload 包入自描述明文 envelope：magic、schema version、payload length、payload 和 SHA-256 校验。整个 envelope 使用 `CryptProtectData` 加密，设置 `CRYPTPROTECT_UI_FORBIDDEN`，不使用 `CRYPTPROTECT_LOCAL_MACHINE`，不弹 UI、不提供额外 entropy。读取通过 `CryptUnprotectData` 后必须再次验证 envelope 和校验；敏感临时缓冲区用后清零，DPAPI 输出用 `LocalFree`。

加密后的完整快照由专用 worker 写入 `QSaveFile`，保持默认 `setDirectWriteFallback(false)`，只有 `commit()` 成功才替换旧文件。worker 按 revision 合并排队写入，只保证最新快照最终落盘。正常退出最多等待最新 revision 10 秒；成功后退出，失败或超时则取消尚未 commit 的临时文件、保留 last-known-good、记录诊断并退出。

**Rationale**: DPAPI 提供当前 Windows 用户边界；应用层 envelope 可发现 DPAPI 偶尔未报错的损坏输出。长度前缀二进制格式避免 JSON 对控制字符的最坏多倍扩张，也能在构造 `QString` 前执行 1MiB 边界校验。`QSaveFile` 提供同目录临时文件和原子提交。最坏 100MiB 的加密/写入不应阻塞 GUI 线程。

**Failure rule**: 加载、解密或完整性校验失败时，不用空集合自动覆盖原文件；会话内新捕获仍可显示，但自动保存进入 `ReadLocked`。用户明确确认“清空历史”时可将其作为重置不可读存储的授权。

**Alternatives rejected**:

- 明文或仅混淆: 不满足同用户加密要求。
- `CRYPTPROTECT_LOCAL_MACHINE`: 同机器其他用户可解密，不符合当前用户边界。
- 直接覆盖文件: 进程崩溃或磁盘故障可能丢失 last-known-good。
- GUI 线程同步写整个快照: 最坏输入下会违反交互性能目标。
- append-only 日志: 去重、淘汰和密文恢复更复杂，100 项规模收益不足。
- JSON payload: 易于调试但密文内部不需要可读性，且大量控制字符会显著扩张中间缓冲区。

**Official sources**: [CryptProtectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata), [CryptUnprotectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptunprotectdata), [Microsoft DPAPI example](https://learn.microsoft.com/en-us/windows/win32/seccrypto/example-c-program-using-cryptprotectdata), [QSaveFile 6.10](https://doc.qt.io/qt-6.10/qsavefile.html)

## 6. 启动合并、数据位置和运行隔离

**Decision**: 历史文件固定为现有 `AppSettings::fileName()` 父目录下的 `clipboard-history.dat`，从而使两个稳定安装入口共享同一数据，同时自然继承 test-mode 的隔离 settings 路径。生产环境不提供覆盖历史路径的 CLI 或环境变量；核心测试可直接构造显式临时路径。

普通模式先建立空模型、记录监听基线并启动异步加载。加载期间的新捕获立即进入会话模型并可见，但不写盘；加载成功后把磁盘项目按原顺序合并到较新的会话项目之后，按全文去重并截断到 100 条，再排队保存合并结果。加载失败则保留会话项目但锁止自动写入。外部文件模式不构造 store、不读取文件、不注册监听器，也不注册历史命令。

**Rationale**: 跟随 settings 目录满足现有稳定副本共享语义和测试隔离，不扩展 settings schema。启动异步加载避免最坏快照阻塞窗口，同时保证加载中的新变化在 500ms 内可见且不会被旧磁盘快照覆盖。

**Alternatives rejected**:

- 独立使用 `QStandardPaths::StateLocation`: 会改变现有两个稳定入口共享配置的直觉，并需额外测试路径覆盖入口。
- 将大文本放入 `settings.ini`: 混淆配置 schema 和加密大对象职责。
- 等待加载完成才监听: 启动期间可能漏掉剪贴板变化。

**Official source**: [QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html)

## 7. 编辑器回溯与 dirty 语义

**Decision**: Controller 保存 `editorBaselineText`。普通 show 成功载入剪贴板、外部文件载入和历史回溯成功后均更新 baseline；dirty 定义为当前全文与 baseline 不相等，因此用户改回原文后不提示。请求载入历史时，若 dirty 且目标不同则显示确认；取消不改变文本、光标、选择、面板、搜索或选中项。确认后替换全文、光标置末、清空撤销历史、更新 baseline、折叠面板并聚焦编辑器。

Esc/Ctrl+S 仍通过现有成功提交路径写剪贴板并隐藏；其成功写入记录新的或置顶的历史项。Ctrl+W 继续放弃、不写剪贴板也不新增历史。原历史记录永不因编辑器修改而原地变化。

**Rationale**: baseline 比单向 dirty 标志准确，能处理撤回到原文。清空 undo 防止用户绕过已确认的“放弃当前修改”。

## 8. 自动化验证边界

**Decision**: 把 listener 生命周期、sequence、直接 Win32 读写和格式解析统一封装为 `ClipboardGateway`。生产实现访问系统剪贴板；test mode 使用内存实现，所有 lifecycle、read、write、变化注入和发送结果均不访问 OS。既有 `testSetClipboard` 只设置虚拟当前值且不产生历史事件；新命令显式注入变化。故障用确定性的 test-only fault injection 覆盖 listener 注册、sequence 竞态、read/decrypt/encrypt/write，不依赖 ACL、文件锁或系统时序。

external file mode 不启动 resident IPC。其隔离验收通过仅在 `--test-mode --wait` 接受的 `SCRATCHEDITOR_EXTERNAL_TEST_STATUS_FILE` 输出一次临时 JSON，报告 history availability、store path、命令注册和 QML panel Loader 状态；该文件必须位于 runner 验证过的临时目录，不构成生产接口。新增历史测试命令的生产拒绝通过无窗口 dispatch 单元 fixture 验证，不启动普通实例。

新增模型/存储测试 target 和隔离集成 runner，扩展 window-ui、editing、perf；不把历史验收加入现有会真实操作系统剪贴板的 `system_main.cpp`。完整系统回归只有在用户明确批准真实剪贴板测试后执行。

**Rationale**: 维持现有测试语义可避免 show/cursor 回归被历史事件污染；统一 gateway 同时修复 test-mode 提交路径仍可能写真实剪贴板的现状。external 状态文件和无窗口 gate fixture 分别解决外部模式无 IPC、普通实例不可安全启动的可观察性问题。

**Alternatives rejected**:

- 只给监听器加测试替身: Esc/Ctrl+S 仍会触碰真实剪贴板。
- 用真实剪贴板跑历史集成测试: 违反宪章的用户环境隔离要求。
- 用文件权限制造故障: 时序不稳定且平台行为差异大。
