# 阶段 5：项目收尾验收报告

验收时间：2026-08-02（Asia/Shanghai）

## 结论

阶段 5 已通过。项目目录、测试资料、工作文档和最终验收证据已集中整理；项目说明、构建与测试入口、AHK 集成边界和各阶段报告均已建立索引。阶段 4/3/2 功能回归、隔离 AHK 持久 IPC、阶段 1 全量性能回归和项目一致性审计全部通过。仓库以 `main` 为初始分支，阶段 1 至阶段 5 的可交付内容纳入初始提交。

本阶段没有修改原始 `KeysRedirect.ahk`，没有删除旧 AHK 编辑器模块，也没有进入阶段 6。

## 目录与资料整理

- 根目录只保留构建入口、迁移文档、项目 README 和 Git 忽略规则。
- 最终机器可读证据集中在 `artifacts/baselines/`；调优过程中的历史结果完整移至本地 `artifacts/archive/intermediate/`，没有删除。
- 工具安装日志移至本地 `artifacts/tooling/`，构建输出、工具链、开发链接、历史中间结果和工具日志均由 `.gitignore` 排除。
- AHK 隔离测试副本移至 `tests/fixtures/`；可交付的迁移参考副本保留在 `integration/`。
- 新增 `docs/README.md`、`tests/README.md`、`integration/README.md` 和 `artifacts/baselines/README.md`，分别索引报告、测试入口、AHK 边界和最终基线。
- 根 `README.md` 已更新为当前架构、功能范围、构建运行方式、测试命令、配置位置、回退策略和阶段状态。

## 最终回归

功能结果：`artifacts/baselines/stage5-functional-20260802-131459.json`

AHK IPC 结果：`artifacts/baselines/stage5-ahk-ipc-20260802-131519.json`

性能结果：`artifacts/baselines/stage5-performance-20260802-131542.json`

| 检查项 | 结果 |
|---|---:|
| 阶段 4 设置、主题、字体、动画和集中配置 | 通过 |
| 阶段 3 编辑器功能回归 | 通过 |
| 阶段 2 窗口与 IPC 合约回归 | 通过 |
| 隔离 AHK 副本的持久 `show` / `hide` IPC | 通过 |
| 延迟功能与 Markdown 预览未注册 | 通过 |
| Qt WebEngine / WebView / Chromium 引用 | 无 |
| 独立设置文件与用户实例隔离 | 通过 |
| 原始 AHK 内容保护 | 通过 |

原始 `D:\Documents\AutoHotkey\KeysRedirect.ahk` 验收前后 SHA-256 均为：

`8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9`

## 性能回归

| 指标 | 最终实测 | 门槛 | 结果 |
|---|---:|---:|---:|
| 新进程可用平均值 / 最大值 | 79.26 / 106.97 ms | 最大值 ≤ 300 ms | 通过 |
| 热唤醒 P95 | 17.09 ms | ≤ 50 ms | 通过 |
| 10 万字输入到帧 P95 | 15.87 ms | ≤ 16.667 ms | 通过 |
| 空闲 CPU | 0% | ≤ 0.5% | 通过 |
| 隐藏工作集 | 39.56 MB | ≤ 80 MB | 通过 |
| 动画 | 60.01 FPS / P95 16.91 ms | ≥ 55 FPS / ≤ 20 ms | 通过 |
| 微软拼音真实候选提交 | `你好` | 精确中文提交 | 通过 |

11 个命令、原生 Markdown 高亮、暗色首帧、CJK 字形、高 DPI、窗口置顶和无边框合约均同时通过。性能运行期间设置页保持未加载。

## 项目审计与 Git

- `scripts/run-stage5-audit.ps1` 检查必需文件、最终基线 JSON、根目录整洁、资料集中、PowerShell 语法、集中配置实现、浏览器内核排除、延迟功能排除、AHK 回退边界、原始 AHK 哈希及 Git 忽略/空白/工作区状态。
- 最终审计文件为 `artifacts/baselines/stage5-audit.json`。
- Git 初始分支为 `main`，初始提交信息为 `Complete ScratchEditor migration through stage 5`。提交哈希以仓库最终状态为准，避免在提交内容中形成自引用。
- `.tools/`、`build/`、`dev-links/`、`artifacts/archive/` 和 `artifacts/tooling/` 不进入版本历史；最终基线会进入版本历史。

## 构建与交付

- 当前独立部署产物位于 `build/stage4/ScratchEditor.exe`，SHA-256 为 `30A1A5645AAE5C714D9531320E3892CBA92240DAEDFF03ACDC340C4EAFED831D`。
- 部署目录包含 160 个文件，约 53.53 MiB；已通过移除 Qt/MinGW 工具链 PATH 后的独立运行验证。
- 构建入口为 `scripts/build.ps1 -Preset stage4`；阶段 5 功能、AHK IPC、性能和项目审计均有独立脚本入口。
- Qt 部署资源仍会输出已知的 `libpng iCCP` 警告，不影响运行或验收结果。

## 后续边界

阶段 5 完成后停止。阶段 6 会修改原始 AHK、移除旧 GUI 实现，只能在用户明确批准后执行；批准前继续保留旧编辑器、Qt 开关和启动失败时的纯剪贴板回退路径。
