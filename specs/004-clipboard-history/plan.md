# Implementation Plan: 剪贴板历史

**Branch**: `004` | **Date**: 2026-08-09 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/004-clipboard-history/spec.md`

## Summary

为常驻剪贴板编辑模式增加 Windows 文本剪贴板监听、最多 100 条的本地加密历史，以及可搜索、查看、回溯、删除和清空的左侧折叠面板。实现复用 `EditorController` 已有的原生事件过滤器，但 listener 注册、sequence、Win32 剪贴板读取/写入、排除格式和正文解析全部由统一 `ClipboardGateway` 封装；历史业务由独立的 `ClipboardHistoryModel` 管理，版本化快照经当前用户 DPAPI 加密并由 `QSaveFile` 异步原子保存。所有自动化验证统一走内存剪贴板网关和隔离文件，不读取或修改用户剪贴板，不修改用户配置、AHK 或稳定安装副本。

## Technical Context

**Language/Version**: C++20、QML，Qt 6.10.2

**Primary Dependencies**: Qt Core/Gui/Network/Qml/Quick；Windows User32 剪贴板 API；Windows DPAPI (`Crypt32`)

**Storage**: `settings.ini` 同目录下的版本化 `clipboard-history.dat`；整个快照使用当前 Windows 用户范围的 DPAPI 加密，通过 `QSaveFile` 原子替换

**Testing**: 现有自定义 C++ 测试程序与 PowerShell runner；新增模型/存储、原生缓冲解析、listener/sequence 故障和 IPC gate 单元测试，以及隔离的 test-mode 集成测试，并扩展 editing、external、window-ui、perf 测试

**Target Platform**: Windows 11 桌面，MinGW 13.1、CMake 3.25+、Ninja

**Project Type**: 单体 Windows 桌面应用

**Performance Goals**: 合格文本从剪贴板变化到已打开面板可见的 p95 不超过 500ms；100 条历史下展开、搜索和置顶更新的 p95 不超过 100ms；动画关闭时状态切换无额外动画延迟

**Constraints**: 仅纯文本；单条 UTF-8 编码不超过 1MiB；最多 100 个精确唯一文本；触发区/展开/收起常量为 12px/100ms/250ms；面板宽度为 `clamp(window.width/3, 200, 360)`；编辑器可见宽度至少 320px；所有文本位置和字符数沿用 Qt UTF-16 code unit；不增加生产 IPC/CLI/AHK 入口；外部文件模式完全禁用历史；GUI 线程不得执行最坏 100MiB 快照的加密和磁盘写入；正常退出等待最新 revision 最多 10 秒

**Scale/Scope**: 单用户、单机、一个常驻实例；历史明文上限约 100MiB，界面最多渲染 100 个摘要项

## Constitution Check

*GATE: Phase 0 研究前检查，并在 Phase 1 设计完成后复核。*

| 宪章门禁 | 设计证据 | Phase 0 前 | Phase 1 后 |
|---|---|---:|---:|
| 不破坏用户环境 | 开发构建只用 `-SkipLocalInstall`；测试使用内存网关、临时 settings/history、native-access 计数和静态 API 边界审计；不读取或修改用户剪贴板，不修改 AHK 或稳定副本 | PASS | PASS |
| 架构与运行边界 | C++ 负责监听、模型、存储和生命周期；QML 只负责面板交互；外部文件模式不创建模型、存储或监听器 | PASS | PASS |
| UTF-16 与性能 | 字符数使用 `QString::size()`；模型不把全文复制到 delegate；筛选最多线性扫描 100 条；持久化在 worker 中合并修订 | PASS | PASS |
| 测试先行 | 任务阶段先添加稳定失败的模型、存储、Win32 缓冲解析、listener/sequence 故障、IPC gate、集成和 UI 验收，再实现对应代码；不通过放宽断言消除回归 | PASS | PASS |
| 最小改动与文档 | 复用现有窗口 HWND、事件过滤器、命令注册表和快捷键存储；完成后更新 `README.md`、`tests/README.md` 并检查 `docs/README.md` | PASS | PASS |
| API 真实性 | Win32、DPAPI、Qt API 均在 [research.md](research.md) 中以 Microsoft/Qt 官方资料核验 | PASS | PASS |
| 提交与部署 | 本计划不提交、不部署；若后续要求提交，使用简体中文规范提交信息并单独确认部署 | PASS | PASS |

没有需要豁免或额外论证的宪章冲突。

## Project Structure

### Documentation (this feature)

```text
specs/004-clipboard-history/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── clipboard-history-ui.md
│   └── test-mode-ipc.md
├── checklists/
│   └── requirements.md
└── tasks.md                    # dependency-ordered implementation tasks
```

### Source Code (repository root)

```text
src/
├── clipboardgateway.h/.cpp             # listener、sequence、读写、格式解析与测试内存边界
├── clipboardhistorymodel.h/.cpp        # 去重、排序、筛选、删除和容量限制
├── clipboardhistorystore.h/.cpp        # DPAPI、版本化快照和异步原子保存
├── editorcontroller.h/.cpp             # 生命周期、监听、编辑器载入与确认流程
├── editorcommandregistry.h/.cpp        # 可配置且默认无快捷键的 UI 命令
└── appsettings.h/.cpp                  # 复用现有 settings 路径，不扩 schema

qml/
└── Main.qml                            # 左侧折叠面板、搜索、列表和确认层

tests/
├── clipboardhistory_main.cpp           # 模型、边界、存储和故障单元测试
├── editing_main.cpp                    # 命令和编辑提交/放弃回归
├── window_ui_main.cpp                  # 面板布局、焦点、键鼠与确认验收
├── perf_main.cpp                       # 100 条历史的交互/捕获 p95
└── README.md

scripts/
├── run-clipboard-history-tests.ps1     # 隔离进程重启、密文和损坏恢复测试
├── run-editing-tests.ps1
├── run-window-ui-tests.ps1
└── run-perf-tests.ps1

CMakeLists.txt                           # 新源文件、测试目标和 crypt32 链接
README.md                                # 功能、隐私、数据位置、命令和验收
docs/README.md                           # 完成后检查索引是否需要补充
```

**Structure Decision**: 保持现有单体目录和 C++/QML 职责边界。新增三个职责单一的 C++ 组件，避免继续把历史集合、加密 I/O 和测试替身堆入已较大的 `EditorController`；不引入数据库、Qt Concurrent、第二个窗口或第二套消息循环。

## Phase 0: Research Decisions

详细决策、依据与被拒绝方案见 [research.md](research.md)。关键结论是复用真实窗口接收 `WM_CLIPBOARDUPDATE`，但所有 listener/sequence/剪贴板 API 经 gateway；同一次 Win32 读取中判断排除格式和文本；受控写入先记录一次并以序列号一次性抑制回送通知；历史以轻量模型角色暴露；加密快照由后台 worker 合并最新修订并原子提交。

## Phase 1: Design Outputs

- [data-model.md](data-model.md)：历史条目、集合、捕获候选、持久化快照、存储健康状态和面板状态。
- [contracts/clipboard-history-ui.md](contracts/clipboard-history-ui.md)：生产可见能力、Controller/QML 边界、布局和编辑行为。
- [contracts/test-mode-ipc.md](contracts/test-mode-ipc.md)：仅 test mode 可用的虚拟剪贴板、故障注入和 UI 可观测契约。
- [quickstart.md](quickstart.md)：全程不部署、不访问真实剪贴板的构建与验收顺序。

## Implementation Order

1. 先建立模型、序列化/DPAPI、原子写、Win32 缓冲解析、listener/sequence 故障、IPC gate 和内存剪贴板网关的失败测试。
2. 实现 `ClipboardHistoryModel`、`ClipboardHistoryStore` 和统一 `ClipboardGateway`，跑核心测试。
3. 接入 Controller 生命周期、监听、命令、dirty baseline 和 test-mode 契约，跑隔离集成与 editing 测试。
4. 实现 QML 面板、布局、焦点、键鼠和确认交互，跑 window-ui 测试。
5. 增加性能测量并处理回归；最后更新正式文档并按 quickstart 完整复验。
