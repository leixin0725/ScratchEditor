# Tasks: 剪贴板历史

**Input**: Design documents from `/specs/004-clipboard-history/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: 本功能受项目宪章“测试先行”约束。每个用户故事先添加能够稳定失败、输出 actual/expected 的独立检查，再实现对应行为。

**Organization**: 任务按用户故事分组。US1 提供可独立交付的记录与浏览 MVP；US2、US3、US4 均在 US1 的集合/持久化基础上增加独立可验收能力。

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 在所列前置任务完成后，可与相邻任务并行，且不修改同一文件。
- **[Story]**: 对应 [spec.md](spec.md) 中的用户故事。
- 所有构建和进程测试使用 `editing`/`window-ui` preset、`-SkipLocalInstall`、唯一 server 和临时 settings/history；禁止读取、写入或快照真实剪贴板和稳定安装副本。

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: 建立可编译的组件边界和隔离测试入口，不实现任何历史业务行为。

- [x] T001 创建 `src/clipboardgateway.h`、`src/clipboardgateway.cpp`、`src/clipboardhistorymodel.h`、`src/clipboardhistorymodel.cpp`、`src/clipboardhistorystore.h`、`src/clipboardhistorystore.cpp` 与 `tests/clipboardhistory_main.cpp` 的最小可编译骨架，并在 `CMakeLists.txt` 注册生产源、可链接 gateway/model/store 及无窗口 command-gate fixture 的 `ScratchEditorClipboardHistoryTests` 目标、安装目标以及 Windows `crypt32` 链接
- [x] T002 [P] 创建 `scripts/run-clipboard-history-tests.ps1` 隔离 runner 骨架，提供 `BuildSubdirectory`/`ServerName` 参数、唯一临时 settings/history 目录、解析后的路径边界检查、仅清理自身 PID 的 `finally` 逻辑，且不得调用 `ScratchEditor.exe --quit`

**Checkpoint**: `editing -SkipLocalInstall` 可配置并编译空测试目标，runner 尚不包含行为验收。

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: 统一所有剪贴板读写的生产/测试边界，先修复 test mode 提交仍可能触碰真实剪贴板的基础风险。

**⚠️ CRITICAL**: 本阶段完成前不得开始任何用户故事实现。

- [x] T003 在 `tests/clipboardhistory_main.cpp` 添加内存 `ClipboardGateway` 契约失败测试，覆盖 monitoring lifecycle 不访问 OS、设置虚拟当前值不发事件、显式注入保留 kind/sequence/排除元数据、受控写入只更新虚拟值、发送只更新 delivery sink、backend=`memory` 且 native access attempt=0，并为每个边界输出 actual/expected
- [x] T004 实现 `src/clipboardgateway.h` 与 `src/clipboardgateway.cpp` 的统一接口：生产 gateway 拥有 listener 注册/注销、sequence、Win32 read/write/delivery，test-mode 内存 gateway 实现同一 lifecycle 和捕获候选接口，使 T003 通过且不引入第二个消息窗口
- [x] T005 [P] 在 `tests/editing_main.cpp` 添加失败回归，证明 test mode 的 show、Esc、Ctrl+S、Ctrl+W、`testClipboard` 与 `testDeliveredText` 全程只使用虚拟剪贴板，且既有 `testSetClipboard` 不隐式产生变化事件
- [x] T006 将 `src/editorcontroller.h`、`src/editorcontroller.cpp` 和 `src/editorcommandregistry.cpp` 中现有 clipboard read/write、show、commit、delivery 与命令处理统一路由到 `ClipboardGateway`，保持生产有限重试和既有编辑快捷键语义并使 T005 通过

**Checkpoint**: 所有后续历史自动化均可建立在内存剪贴板上，现有 test-mode 编辑回归不再访问 OS clipboard。

---

## Phase 3: User Story 1 - 自动记录并浏览剪贴板历史 (Priority: P1) 🎯 MVP

**Goal**: 常驻实例监听启动后的合格纯文本变化，按最新优先、精确去重和 100 条上限展示，并用当前 Windows 用户 DPAPI 加密持久化；外部文件模式完全无历史访问。

**Independent Test**: 在隔离实例启动监听后显式注入多段文本，已展开左栏在 500ms 内显示最新项；排除/空/非文本/超限内容不出现；重启后顺序和全文恢复，密文不含明文，external file mode 不创建或访问 history store。

### Tests for User Story 1

> **Write these tests first and verify they fail before implementation.**

- [x] T007 [P] [US1] 在 `tests/clipboardhistory_main.cpp` 添加先失败测试，覆盖无 NUL/奇数字节/声明越界/超限的纯 Win32 UTF-16 缓冲解析、listener 注册失败、sequence 读取期间变化、空/非文本/读取失败、两种排除标记及畸形 DWORD、UTF-8 1MiB 边界、精确 Unicode/空白/换行去重、重复保 ID 并置顶、101 条淘汰、UTF-16 字数、二进制 schema/长度校验、DPAPI roundtrip、密文无明文、损坏密文、read/decrypt/encrypt/write fault、原子写与 last-known-good，并以可控阻塞 worker/测试时钟断言 shutdown 等待达到 10 秒上限后放弃未提交 revision 且不覆盖 last-known-good；同一无窗口 fixture 以 `testMode=false` 断言全部新增 test command 返回 unsupported
- [x] T008 [P] [US1] 在 `scripts/run-clipboard-history-tests.ps1` 添加 test-mode IPC 失败场景，覆盖隐藏窗口期间 `testEmitClipboardChange` 仍捕获、`testSetClipboard` 不捕获、listener 注册失败/恢复、sequenceRace 重试、排除原因、单次 500ms 内可见、连续 10 次正常 quit/restart 后全文/稳定 ID/顺序逐轮一致、密文无明文、损坏后 `ReadLocked` 且不覆盖、明确 reset 只清隔离文件，并检查 history 路径位于临时目录、backend=`memory`、native access attempt=0
- [x] T009 [P] [US1] 在 `tests/window_ui_main.cpp` 添加基础历史栏失败验收，覆盖默认折叠、12px 内侧触发区、导出的 100/250ms timer 常量、短于阈值不展开/返回面板不收起、最新优先的多行摘要/本地化时间/UTF-16 字数、面板内存储错误提示，以及单次捕获到下一 QML 帧可见 `<=500ms`
- [x] T010 [P] [US1] 在 `tests/externaleditorprocess_main.cpp` 添加 external file mode 失败回归，通过临时 `SCRATCHEDITOR_EXTERNAL_TEST_STATUS_FILE` 断言 history unavailable、store path 空、命令未注册、panel Loader 未激活、memory backend/native attempt=0，并比较隔离历史文件前后 hash 不变，同时保持原有 UTF-8 文件读写与退出码

### Implementation for User Story 1

- [x] T011 [P] [US1] 在 `src/clipboardhistorymodel.h` 与 `src/clipboardhistorymodel.cpp` 实现 `ClipboardHistoryItem`、最多 100 条的最新优先集合、稳定不透明 ID、精确全文去重/时间刷新/置顶/淘汰、UTF-8 大小验证、UTF-16 字数和仅含摘要/时间/字数/ID 的 `QAbstractListModel` roles
- [x] T012 [P] [US1] 在 `src/clipboardhistorystore.h` 与 `src/clipboardhistorystore.cpp` 实现固定字节序的长度前缀 payload、magic/schema/revision/count/字段上限、SHA-256 envelope、严格解码和拒绝尾随/重复/超限数据，使 T007 的 codec 边界通过
- [x] T013 [US1] 在 `src/clipboardhistorystore.h` 与 `src/clipboardhistorystore.cpp` 实现当前用户范围 `CryptProtectData`/`CryptUnprotectData`、`CRYPTPROTECT_UI_FORBIDDEN`、敏感缓冲清零、后台 worker revision 合并、默认禁止 direct fallback 的 `QSaveFile` 原子提交、退出最多等待最新 revision 10 秒、超时/失败丢弃未 commit 临时文件并保留 last-known-good，以及 `Loading/Ready/WritePending/WriteFailed/ReadLocked` 状态机
- [x] T014 [US1] 在 `src/editorcontroller.h` 与 `src/editorcontroller.cpp` 仅为普通常驻模式创建 history model/store，从 `AppSettings::fileName()` 父目录派生 `clipboard-history.dat`，异步加载期间立即展示会话捕获，成功后按“会话较新”规则合并磁盘项，并独立暴露 `historyAvailable/Healthy/Error/Count/StoreState` 而不让 clipboard success 清除 store 错误
- [x] T015 [US1] 在 `src/clipboardgateway.h`、`src/clipboardgateway.cpp`、`src/editorcontroller.h` 与 `src/editorcontroller.cpp` 让 gateway 使用真实 HWND 注册/注销 listener、记录启动 sequence，并在一次 `OpenClipboard` 中校验两种排除格式和严格 `CF_UNICODETEXT`；Controller 只接收 `WM_CLIPBOARDUPDATE`、比较 gateway 读取前后 sequence 并调度有限异步重试，且 `src/editorcontroller.cpp` 不直接调用任何 clipboard Win32 API
- [x] T016 [US1] 在 `src/editorcontroller.cpp` 与 `src/main.cpp` 按 `contracts/test-mode-ipc.md` 实现可单元测试的 history command gate/无窗口 dispatch seam，将 `testEmitClipboardChange`、`testClipboardHistoryState`、`testResetClipboardHistory`、`testSetClipboardHistoryFault`、`testRestartClipboardMonitoring`、`testWaitForClipboardHistoryIdle` 及 status 字段注册为 `Gate::Test`，支持 listenerRegistration/sequenceRace fault、backend/native attempt 观测和仅 `--test-mode --wait` 的外部状态文件，并保持 `testSetClipboard` 只设值不发事件
- [x] T017 [P] [US1] 在 `qml/Main.qml` 实现普通模式专用的默认折叠左侧面板、12px 内侧触发条、100/250ms hover timers、最新优先 ListView、多行摘要/时间/字数、loading/empty/error 状态和共用 QML test action dispatcher；使用 `Loader.active=historyAvailable`，external 状态文件可观察 panel Loader 未激活
- [ ] T018 [US1] 按 `specs/004-clipboard-history/quickstart.md` 使用 `editing` 与 `window-ui` 的 `-SkipLocalInstall` 构建，依次运行 `ScratchEditorClipboardHistoryTests.exe`、`scripts/run-clipboard-history-tests.ps1`、US1 window-ui 与 external process 回归，记录失败详情并确认 MVP 独立验收通过

**Checkpoint**: US1 可独立演示和验收：后台捕获、加密恢复及基础浏览闭环完成。

---

## Phase 4: User Story 2 - 回溯并编辑历史文本 (Priority: P2)

**Goal**: 单击只选中，双击或 Enter 把历史全文载入编辑器；dirty 缓冲先确认，取消完整保持状态；成功提交只记录一次新结果，放弃不改变历史。

**Independent Test**: 选中旧项并通过双击/Enter 载入，验证折叠、焦点、光标和 undo baseline；dirty 取消逐项保持状态、确认才覆盖；修改后 Esc/Ctrl+S 产生一次最新历史且原项不变，Ctrl+W 不新增也不写虚拟剪贴板。

### Tests for User Story 2

- [x] T019 [P] [US2] 在 `tests/window_ui_main.cpp` 添加失败验收，覆盖单击只选择、稳定 ID 选择、双击/Enter 请求载入、dirty 判定与改回 baseline 不提示、取消保持文本/光标/选择/panel/query/selected ID、确认后光标置末/清 undo/折叠/聚焦编辑器
- [x] T020 [P] [US2] 在 `tests/editing_main.cpp` 添加失败回归，覆盖历史载入后的 Esc 回写、Ctrl+S 发送、Ctrl+W 放弃、原历史项不可变、修改结果按精确去重加入/置顶，以及虚拟 clipboard/delivery sink 的逐项 actual/expected
- [x] T021 [P] [US2] 在 `scripts/run-clipboard-history-tests.ps1` 添加受控写入集成失败场景：成功写入立即记录一次，再注入相同 sequence/text 必须返回 `selfWriteNotification` 且 revision/时间/顺序不再变化；失败提交与放弃不得新增或重排

### Implementation for User Story 2

- [x] T022 [P] [US2] 在 `src/editorcontroller.h` 与 `src/editorcontroller.cpp` 增加 `editorBaselineText`、pending history ID、`request/confirm/cancelLoadClipboardHistory` 和按 ID 取全文；普通 show 与成功历史载入更新 baseline，确认载入设置全文/末尾光标/清 undo，取消不修改任何编辑器或面板状态
- [x] T023 [P] [US2] 在 `qml/Main.qml` 实现稳定 ID 选择、单击/双击/Enter 行为、dirty 载入确认层、确认/取消 action、成功载入后的折叠与编辑器聚焦，并保证确认层的 Escape 只取消确认
- [x] T024 [US2] 在 `src/editorcontroller.h` 与 `src/editorcontroller.cpp` 将 Esc/Ctrl+S 的成功 clipboard write/delivery 立即送入模型，建立带短超时的 sequence+文本指纹一次性 suppression token；只抑制一个匹配回送，Ctrl+W/失败写入不记录，普通 Copy/Cut 不被全局屏蔽
- [x] T025 [US2] 在 `src/editorcontroller.cpp` 与 `qml/Main.qml` 补齐 US2 所需 test-only UI action/status 可观测字段，确保 IPC dispatcher 调用真实 QML action 并等待下一帧，而不是直接修改模型、编辑器或确认状态
- [x] T026 [US2] 运行 `tests/editing_main.cpp`、`tests/window_ui_main.cpp` 与 `scripts/run-clipboard-history-tests.ps1` 的 US2 检查并按 `specs/004-clipboard-history/contracts/clipboard-history-ui.md` 逐项确认回溯、确认、提交、发送和放弃语义

**Checkpoint**: US2 可独立验收，历史文本能够安全回溯和再编辑，原记录及放弃语义保持不变。

---

## Phase 5: User Story 3 - 快速定位并适配不同窗口尺寸 (Priority: P3)

**Goal**: 在最多 100 条历史中搜索并键盘定位；通过无默认快捷键的命令打开；宽窗口挤压、窄窗口覆盖且编辑器可见宽度不低于 320px；满足交互性能目标。

**Independent Test**: 对大小写不同的全文子串搜索并用方向键/Enter 载入；920px 窗口使用 `clamp(window.width / 3, 200, 360)` 计算的 push，500px 使用 overlay；动画关闭立即切换；100 条下展开/筛选/置顶的下一帧 p95 不超过 100ms。

### Tests for User Story 3

- [x] T027 [P] [US3] 在 `tests/clipboardhistory_main.cpp` 添加失败测试，覆盖大小写不敏感全文子串、最新优先结果、query 不改变持久化顺序、筛选/重复置顶后的稳定 ID 选择、选中项消失时选择最近可见项，并验证过滤索引不复制全文 role
- [x] T028 [P] [US3] 在 `tests/window_ui_main.cpp` 添加失败验收，覆盖命令打开搜索焦点与离开锁定、方向键/Enter、折叠后清 query、920px push、500px overlay、200–360px clamp、编辑器 `>=320px`、触发条不覆盖 resize 区和 animations off duration 0
- [x] T029 [P] [US3] 在 `tests/editing_main.cpp` 添加常驻模式 `clipboardHistory` 命令失败回归，验证标题/类别/uiCommand、默认空快捷键以及现有通用 shortcut 配置持久化与重置；external 命令缺失只由 T010 的状态文件验证
- [x] T030 [P] [US3] 在 `tests/perf_main.cpp` 与 `scripts/run-perf-tests.ps1` 添加至少 20 次捕获到已打开面板下一 QML 帧可见的 p95 `<=500ms`，以及 100 条历史下至少 20 次展开、筛选、重复置顶的下一帧 p95 `<=100ms`，并保留现有 idle CPU、working set 与动画性能断言

### Implementation for User Story 3

- [x] T031 [P] [US3] 在 `src/clipboardhistorymodel.h` 与 `src/clipboardhistorymodel.cpp` 实现大小写不敏感全文子串过滤、最多 100 个 `visibleIndices` 的线性重建、稳定 ID 选择恢复和最近可见项回退，不向 QML role 复制完整正文
- [x] T032 [P] [US3] 在 `src/editorcommandregistry.h`、`src/editorcommandregistry.cpp`、`src/editorcontroller.h` 与 `src/editorcontroller.cpp` 仅为历史可用模式注册 `clipboardHistory` UI command（标题“打开剪贴板历史”、类别“界面”、默认 shortcut 空），复用现有快捷键存储，并接通 filter、command-open focus 与测试 status
- [x] T033 [US3] 在 `qml/Main.qml` 实现搜索框、键盘导航、命令打开焦点锁、折叠清 query，以及宽度 `clamp(window.width/3, 200, 360)`、编辑内容区扣除面板后剩余 `>=320px` 时 push/否则 overlay 的动画布局；复用现有 `animationsEnabled` 和统一 transition duration
- [x] T034 [US3] 在 `src/editorcontroller.cpp` 与 `qml/Main.qml` 完成 `historyPanelOpen/Overlay/Width/editorVisibleWidth/historyQueryFocused/historySelectedId` 等 test-mode 下一帧状态同步，使 window-ui/perf 测量观察最终界面而非 IPC handler 时长
- [x] T035 [US3] 运行 `tests/clipboardhistory_main.cpp`、`tests/editing_main.cpp`、`tests/window_ui_main.cpp` 与 `tests/perf_main.cpp` 的 US3 检查；若性能失败，只在 `src/clipboardhistorymodel.cpp`、`src/editorcontroller.cpp` 或 `qml/Main.qml` 中做有测量证据的线性优化，不放宽门槛

**Checkpoint**: US3 可独立验收，100 条记录仍能快速搜索和键盘载入，所有窗口宽度满足响应式下限。

---

## Phase 6: User Story 4 - 管理本地历史 (Priority: P4)

**Goal**: 删除单条或确认后清空全部内部历史，始终不改变系统/虚拟剪贴板、编辑缓冲区或其他设置；不可读存储仅在明确确认清空后重置。

**Independent Test**: 删除选中项、取消清空、确认清空并逐字比较操作前后的虚拟剪贴板和编辑文本；重启确认删除已持久化；在 `ReadLocked` 中确认清空可重置，未确认时原密文不被覆盖。

### Tests for User Story 4

- [x] T036 [P] [US4] 在 `tests/clipboardhistory_main.cpp` 添加删除/清空失败测试，覆盖按稳定 ID 删除、删除不存在 ID 无副作用、选择回退、clear revision、write failure 保留 last-known-good、`ReadLocked` 未确认不覆盖和明确 reset 后可保存空快照
- [x] T037 [P] [US4] 在 `tests/window_ui_main.cpp` 与 `scripts/run-clipboard-history-tests.ps1` 添加失败验收，覆盖删除无需确认、清空请求/取消/确认、确认层 Escape、编辑器/选择/虚拟 clipboard/delivery/settings 不变、删除后重启不恢复，以及 test reset 不改变虚拟当前值

### Implementation for User Story 4

- [x] T038 [US4] 在 `src/clipboardhistorymodel.h`、`src/clipboardhistorymodel.cpp`、`src/editorcontroller.h` 与 `src/editorcontroller.cpp` 实现 `deleteClipboardHistoryItem`、`request/confirm/cancelClearClipboardHistory`、稳定选择回退、revision 持久化和 `ReadLocked` 明确重置授权，且不调用 clipboard gateway 写入
- [x] T039 [US4] 在 `qml/Main.qml` 实现删除所选项控件、清空入口和轻量确认层；取消完整保持集合/query/选择，确认只更新内部历史，存储失败时保留会话结果并显示 last-known-good 错误语义
- [x] T040 [US4] 在 `src/editorcontroller.cpp` 与 `qml/Main.qml` 补齐 `historyClearConfirmationVisible`、删除/请求清空/确认/取消的 test-only action，并确保 dispatcher 复用生产 QML action
- [x] T041 [US4] 运行 `ScratchEditorClipboardHistoryTests.exe`、`scripts/run-clipboard-history-tests.ps1` 和 `scripts/run-window-ui-tests.ps1` 的 US4 验收，确认 SC-007/SC-008 中剪贴板、编辑器和有效旧文件的意外变化次数均为 0

**Checkpoint**: 四个用户故事全部可独立验证，用户可以安全管理本地历史。

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: 完成跨故事回归、正式文档和隔离交付证据。

- [x] T042 [P] 更新 `scripts/run-final-audit.ps1` 的受检源码/测试清单以包含 clipboard history 新文件和 runner，并静态断言 `src/` 中 `Add/RemoveClipboardFormatListener`、`GetClipboardSequenceNumber`、`Open/CloseClipboard`、`Get/SetClipboardData` 仅出现在 `src/clipboardgateway.cpp`；审计不得读取或写入真实剪贴板
- [x] T043 [P] 更新根 `README.md` 的唯一正式功能说明，记录监听范围、排除规则、100 条/1MiB 边界、数据位置与 DPAPI 隐私、命令与无默认快捷键、外部文件模式、错误恢复和隔离验证命令
- [x] T044 [P] 更新 `tests/README.md` 增加核心测试及 `scripts/run-clipboard-history-tests.ps1` 的隔离执行顺序和真实 clipboard system 测试警告，并检查/按需更新 `docs/README.md` 索引且不复制持续维护的功能细节
- [ ] T045 按 `specs/004-clipboard-history/quickstart.md` 从干净的 `editing`/`window-ui -SkipLocalInstall` 构建执行核心、历史集成、editing、external、window-ui 与 perf 验收；偶发超时最多重跑一次并单独记录，明确不运行会触碰真实剪贴板的 `scripts/run-system-tests.ps1`
- [x] T046 审查 `git diff`、测试 artifact 和临时目录，验证所有隔离状态均为 backend=`memory`、native clipboard access attempt=0，静态 API 边界审计通过，`%LOCALAPPDATA%\ScratchEditor\CodexEditor`、`AhkEditor`、`D:\_Dev\ScratchEditor\dev-links\AutoHotkey\KeysRedirect.ahk`、生产 settings/history 及其他 ScratchEditor PID 均未变化；不得读取或比较真实剪贴板内容，并在交付报告中给出未部署可执行文件的精确路径

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 Setup**: 无依赖，可立即开始。
- **Phase 2 Foundational**: 依赖 Phase 1；完成后才能写任何历史故事实现。
- **Phase 3 US1**: 依赖 Phase 2；它建立所有后续故事共用的集合、存储、监听和基础面板。
- **Phase 4 US2**: 依赖 US1 的稳定 ID、全文按 ID 读取和基础面板；不依赖 US3/US4。
- **Phase 5 US3**: 依赖 US1 的模型和基础面板；不依赖 US2/US4，但与 US2 都会修改 Controller/QML，实际并行时必须协调文件所有权。
- **Phase 6 US4**: 依赖 US1 的模型/持久化；行为上不依赖 US2/US3，但按优先级在其后交付并复用已完成的选择 UI。
- **Phase 7 Polish**: 依赖计划交付的全部用户故事。

### User Story Dependency Graph

```text
Setup → Foundational → US1 (MVP)
                         ├──→ US2
                         ├──→ US3
                         └──→ US4
US2 + US3 + US4 → Polish
```

### Within Each User Story

1. 先完成该故事全部测试任务，并确认新增检查在实现前稳定失败。
2. 模型/codec 在 Controller 与 QML 集成前完成。
3. Controller 契约稳定后再完成最终 QML 联调和 test-only 可观测字段。
4. 运行故事 checkpoint；失败不得通过删除、跳过或放宽断言处理。
5. 只有当前优先级故事通过后，才进入顺序实施的下一故事。

## Parallel Opportunities

- T001 与 T002 可分别准备 CMake/源码骨架和 PowerShell runner。
- Phase 2 中 T003 与 T005 可先在不同测试文件并行建立失败证据；T004/T006 随后按依赖实施。
- US1 的 T007–T010 可并行编写；T011 与 T012 可并行实现；Controller 集成期间 T017 可按已冻结契约独立开发 QML。
- US2 的 T019–T021 可并行编写；T022 与 T023 可分别实现 Controller 和 QML 后再联调。
- US3 的 T027–T030 可并行建立模型、UI、命令和性能失败基线；T031 与 T032 可并行实现。
- US4 的 T036 与 T037 可并行建立核心/UI 失败基线。
- Polish 的 T042–T044 修改不同文档/脚本，可并行执行。

## Parallel Example: User Story 1

```text
Task T007: tests/clipboardhistory_main.cpp 模型/存储失败测试
Task T008: scripts/run-clipboard-history-tests.ps1 IPC/重启/损坏失败场景
Task T009: tests/window_ui_main.cpp 基础历史栏失败验收
Task T010: tests/externaleditorprocess_main.cpp 外部模式隔离失败回归

完成 T007 后并行：
Task T011: src/clipboardhistorymodel.h/.cpp
Task T012: src/clipboardhistorystore.h/.cpp codec
```

## Parallel Example: User Story 2

```text
Task T019: tests/window_ui_main.cpp 载入/dirty/焦点失败验收
Task T020: tests/editing_main.cpp 提交/发送/放弃失败回归
Task T021: scripts/run-clipboard-history-tests.ps1 自身写入 suppression 场景

接口按 contracts/clipboard-history-ui.md 冻结后并行：
Task T022: src/editorcontroller.h/.cpp baseline 与载入事务
Task T023: qml/Main.qml 选择、激活与确认层
```

## Parallel Example: User Story 3

```text
Task T027: tests/clipboardhistory_main.cpp 搜索和稳定选择测试
Task T028: tests/window_ui_main.cpp 响应式布局/焦点测试
Task T029: tests/editing_main.cpp 命令与快捷键测试
Task T030: tests/perf_main.cpp + scripts/run-perf-tests.ps1 性能门槛
```

## Parallel Example: User Story 4

```text
Task T036: tests/clipboardhistory_main.cpp 删除/清空/存储状态测试
Task T037: tests/window_ui_main.cpp + scripts/run-clipboard-history-tests.ps1 用户流程测试
```

---

## Implementation Strategy

### MVP First (US1 Only)

1. 完成 Phase 1 Setup。
2. 完成 Phase 2 Foundational，验证 test mode 不再触碰真实剪贴板。
3. 完成 Phase 3 US1 的测试、实现与 checkpoint。
4. **STOP AND VALIDATE**：只用隔离历史和虚拟剪贴板演示“监听后捕获 → 左栏浏览 → 加密重启恢复”。
5. 不部署；向用户报告 `build\editing\ScratchEditor.exe` 和 `build\window-ui\ScratchEditor.exe` 的可测试位置。

### Incremental Delivery

1. Setup + Foundational → 安全测试基础。
2. US1 → 自动记录、加密恢复、基础浏览 MVP。
3. US2 → 回溯、确认和编辑闭环。
4. US3 → 搜索、命令、键盘、响应式布局与性能。
5. US4 → 删除和确认清空。
6. Polish → 文档、完整隔离回归和环境审计。

### Execution Discipline

- 同一实现 session 内优先按 T001→T046 顺序处理；仅在 `[P]` 且前置条件满足时并行。
- 不在功能实现中修改 AHK、生产 IPC/CLI、稳定安装副本或用户 history/settings。
- 不以 `scripts/run-system-tests.ps1` 作为本功能自动化验收，因为其遗留流程会接触真实剪贴板。
- 用户未要求提交或部署时，任务完成也不得自动执行 Git commit、release build 或本地安装。

## Notes

- 每项任务必须在完成后才把 `[ ]` 改为 `[x]`，并保留对应失败/通过证据。
- `[P]` 只表示文件和即时依赖允许并行，不消除前置 phase 或故事依赖。
- 全文位置与字符数始终使用 Qt UTF-16 code unit；UTF-8 字节只用于单条 1MiB 和持久化长度校验。
- 真实剪贴板隔离只通过 memory backend、native access attempt 计数和 `src/` API 静态边界证明，不得读取真实内容作前后比较。
- `ClipboardHistoryModel` 的 QML roles 不包含完整正文；全文只通过稳定 ID 载入。
- 所有 test-only 命令必须是 `Gate::Test`；生产 status/IPC 不得泄露历史正文、密文路径或 fault 状态。
