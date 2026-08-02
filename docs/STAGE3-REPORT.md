# 阶段 3：Markdown 编辑增强验收报告

验收时间：2026-08-02（Asia/Shanghai）

## 结论

阶段 3 已通过，并按用户决定明确排除 Markdown 预览。Qt 原生编辑器现已具备 Markdown 语法高亮、常用编辑命令、查找替换、延迟加载命令面板和可持久化快捷键。阶段 2 功能回归与阶段 1 全量性能回归均通过；未进入阶段 4。

## 实现范围

- 使用 `QSyntaxHighlighter` 直接高亮 `QTextDocument`，覆盖标题、引用、列表、任务项、粗体、斜体、行内/围栏代码和链接。
- 统一命令注册表负责文本变换、快捷键、命令面板数据与冲突检查，文本操作保留单次撤销语义。
- 查找支持向前、向后、大小写、跨文档回绕；替换支持当前项和全部替换。
- 查找/替换面板使用纯 Qt Quick；命令面板通过 `Loader` 首次调用时加载。
- 快捷键通过 `QSettings` 持久化，可在命令面板中编辑、检测冲突并恢复默认值。
- 不包含 Markdown 预览命令、预览面板、Qt WebEngine、WebView 或浏览器内核；验收中对不存在的 `togglePreview` 命令进行了显式拒绝检查。

## 命令与默认快捷键

| 命令 | 默认快捷键 |
|---|---:|
| 粗体切换 | `Ctrl+B` |
| 斜体切换 | `Ctrl+I` |
| 标题级别循环 | `Ctrl+Alt+H` |
| 列表切换 | `Ctrl+Shift+L` |
| 任务项切换 | `Ctrl+Alt+T` |
| 引用切换 | `Ctrl+Shift+Q` |
| 行内代码包裹 | `Ctrl+Alt+C` |
| 查找 | `Ctrl+F` |
| 替换 | `Ctrl+H` |
| 命令面板 | `Ctrl+Shift+P` |

## 功能与回归验收

最终结果文件：`artifacts/baselines/stage3-results-20260802-121348.json`

| 检查项 | 结果 |
|---|---:|
| 10 个命令注册与原生高亮器 | 通过 |
| 9 类 Markdown 格式识别 | 通过 |
| 粗体、斜体、标题、列表、任务、引用、代码命令 | 通过 |
| 查找、当前替换、全部替换 | 通过 |
| 查找/替换面板 | 通过 |
| 命令面板延迟加载 | 通过 |
| 快捷键修改、冲突检测与跨进程持久化 | 通过 |
| Markdown 预览排除 | 通过 |
| 阶段 2 完整功能回归 | 通过 |
| 窗口几何跨进程恢复 | 通过 |
| 原 AHK 哈希与仓库状态 | 均未变化 |
| 已运行正式版进程保护 | 通过，PID `39288` 全程保留 |

原始 `KeysRedirect.ahk` 验收前后 SHA-256 均为：

`8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9`

## 阶段 1 性能回归

最终回归结果：`artifacts/baselines/stage3-performance-20260802-121211.json`

| 指标 | 最终实测 | 门槛 | 结果 |
|---|---:|---:|---:|
| 新进程可用最大值 | 78.46 ms | ≤ 300 ms | 通过 |
| 热唤醒 P95 | 41.58 ms | ≤ 50 ms | 通过 |
| 10 万字输入到帧 P95 | 16.04 ms | ≤ 16.667 ms | 通过 |
| 空闲 CPU | 0% | ≤ 0.5% | 通过 |
| 隐藏工作集 | 38.86 MB | ≤ 80 MB | 通过 |
| 动画 | 60.15 FPS / P95 16.91 ms | ≥ 55 FPS / ≤ 20 ms | 通过 |
| 10 万字加载 | 34.50 ms | 无明显阻塞 | 通过 |
| 微软拼音真实候选提交 | `你好` | 精确中文提交 | 通过 |

高亮器对普通文本块使用单次标记扫描和无正则快速路径。热唤醒测试同时修正了 `QLocalSocket` 在请求已进入命名管道后继续等待写通知造成的客户端虚增；最终报告保留完整客户端到帧计时，没有降低任何验收门槛。性能报告的原生进程标准输出也改为显式 UTF-8 读取，中文字段现在是有效 JSON。

## 构建与交付

- 阶段 3 Release 产物位于 `build/stage3/ScratchEditor.exe`，SHA-256 为 `131970E006D86FA957F2C69FDD391DE4BD2230C010749F8252054A701D18E558`。
- `build/stage3` 已通过 `windeployqt` 部署，共 149 个文件、约 50.60 MB；在移除 Qt/MinGW 工具链 PATH 后独立启动、IPC 就绪和正常退出均通过。
- 当前运行中的 `build/release/ScratchEditor.exe` 属于用户正式实例，为避免中断而未覆盖，因此该目录暂时仍是阶段 2 版本；阶段 3 已在独立部署目录就绪。
- 构建预设：`cmake --build --preset stage3`；功能验收入口：`scripts/run-stage3-tests.ps1`；全量性能回归入口：`scripts/run-stage1-tests.ps1 -BuildSubdirectory build\stage3 -ServerName ScratchEditor.Stage3.Perf -ArtifactPrefix stage3-performance`。
- Qt 部署资源仍输出已知的 `libpng iCCP` 警告，不影响运行或验收结果。
