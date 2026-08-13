# ScratchEditor 项目宪章

## 项目定位与技术栈

- Windows 轻量编辑器：Qt 6.10.2 / C++20 / QML，MinGW 13.1、CMake 3.25+、Ninja。
- 编辑核心位于 `src/`，界面在 `qml/`，测试在 `tests/`，脚本在 `scripts/`，历史参考在 `integration/`。
- 工作区工具链位于 `.tools/Qt`，不进入 Git；构建 preset 见 `CMakePresets.json`。
- 产品边界：不提供 Markdown 预览，不使用 WebEngine/WebView 或浏览器内核。

## 文档位置

- 正式文档：`README.md`（架构、构建与验收总览）、`tests/README.md`（测试程序与执行顺序）、`docs/README.md`（文档索引）。
- 历史归档：`docs/archive/`。已完结的功能需求只归档（如 `docs/archive/cjk-features/`），不在宪章中展开。
- 功能细节以正式文档为准；本宪章只保留跨功能、长期有效的约束。
- 项目本体所有持续更新的细节应该且只应来自项目的唯一正式文档 `README.md`。

## 构建与验证

```powershell
# 常规构建（会同步稳定安装副本）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release

# 隔离验证（不触碰 %LOCALAPPDATA%\ScratchEditor 稳定副本）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset editing -SkipLocalInstall
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-editing-tests.ps1 `
  -BuildSubdirectory build\editing -ServerName ScratchEditor.Editing.Validation
```

- 完整回归顺序以 `tests/README.md` 为准（release → system → 外部编辑器 → editing → window-ui → ahk → perf）。
- `quit` 只是 test-mode IPC 命令，不是命令行参数；禁止执行 `ScratchEditor.exe --quit`。
- 每次构建完成但未部署到稳定版时，主动向用户汇报可测试的可执行文件位置。

## 架构与运行边界

- 单实例常驻窗口；生产 IPC 只开放白名单命令，测试命令仅对 `--test-mode` 隔离实例可用。
- 外部编辑使用 `--wait <path>` 文件模式，UTF-8 读回写，绕过剪贴板与常驻 IPC。
- UI/动画设计令牌以 `config/ui.json`（JSONC，可注释）为模板与默认值来源；
  界面强调色以 `config/markdown-style.json` 为单一事实来源；用户覆盖值
  （窗口几何、快捷键、外观、状态面板等）保存在 `settings.ini`（schema 2）；
  测试使用隔离 INI 与隔离 `SCRATCHEDITOR_UI_CONFIG`。

## 必须遵守的规则

### 环境与用户数据保护

1. **不破坏用户环境**：除非用户明确授权对应操作，不得修改项目外的 AHK 文件、剪贴板、用户配置或稳定安装副本（`%LOCALAPPDATA%\ScratchEditor\CodexEditor`、`AhkEditor`）。不得把“未主动写入”或链接来源视为只读保证。
2. **禁止外部用户目录联接**：不得在主工作区、任何 worktree、构建目录或项目临时目录中创建、恢复或保留指向 `%USERPROFILE%\Documents`、`D:\Documents` 或其他项目外用户数据目录的符号链接、junction、挂载点或其他 reparse point；特别禁止重新建立 `dev-links`。测试需要外部基线时，只能使用仓库内受版本控制的夹具，或在用户明确授权后通过参数直接读取单个文件，禁止把外部目录映射进项目。发现此类链接时不得访问、遍历、解除或删除，必须停止并向用户报告。

### 开发迭代与验证

3. **开发迭代必须隔离**：使用 `editing`/`window-ui` preset 并传 `-SkipLocalInstall`。
4. **坐标单位**：一律使用 Qt UTF-16 code-unit 索引，不混用 UTF-8 字节偏移。
5. **性能约束**：编辑核心改动保持线性复杂度；每次编辑最多一次全文读取、一次分析；先收集插入点再倒序写入；禁止在候选循环中重复 `toPlainText()` 或全文扫描。
6. **测试纪律**：关键边界使用独立 check 名并输出 actual/expected 细节；先补能稳定失败的测试再实现；回归失败不得通过删除或放宽断言掩盖；偶发超时最多重跑一次并单独记录。

### 提交、编码与文档维护

7. **原子提交、部署和文档维护**：开发过程中允许自行暂存并创建本地原子提交，无需逐次申请批准；每个提交必须职责单一、可独立回退，提交信息使用简体中文并遵循 GitHub 社区通行规范。禁止自行执行 `git push`，所有远端推送均由用户完成。重要改动完成后检查并更新 `docs/README.md` 索引；已完结的功能需求移入 `docs/archive/`；除非用户明确要求单独部署，否则部署仍按规则 12 等待用户批准。
8. **文件编码**：项目内所有含中文的文本文件统一为 UTF-8（无 BOM）编码，不得混用 GBK/ANSI、UTF-16 或带 BOM 的 UTF-8；新增或编辑文件时保持该约定。

### 沙箱与 Git 权限

9. **沙箱权限**：开发沙箱对 `.git` 目录只读，`git add`/`git commit` 等写入 `.git` 的操作必须使用沙箱外权限（require_escalated）执行，可直接申请，无需先在沙箱内试错。

### 分支与 Worktree 管理

10. **Worktree 共享工具链安全**：worktree 的 `.tools` 可能是指向主工作区 `.tools` 的目录联接（junction）。移除任何非主 worktree 前，必须先在不跟随链接的前提下递归枚举该 worktree 内全部 reparse point；仅允许目标已经核验为主工作区 `.tools` 的顶层 `.tools` junction。发现任何其他 reparse point，或无法证明扫描没有跟随链接时，必须停止并向用户确认，禁止调用 Git 或执行任何递归删除。通过检查后仍必须使用 `scripts/remove-worktree.ps1 -WorktreePath <path>`；该脚本会验证注册状态，只解除 `.tools` 联接，确认共享工具链仍存在后才调用 Git。禁止绕过脚本直接执行 `git worktree remove`，禁止直接移除包含共享 `.tools` 联接的 worktree，也禁止对该联接执行递归删除；Git for Windows 可能沿联接清空主工作区工具链。若脚本因 `.tools` 不是 junction、共享目标不完整或 worktree 未注册而拒绝执行，必须停止并向用户确认，不得自行清理。
11. **开发规模与分支策略**：微调、小功能、缺陷修复和局部重构直接在当前分支推进，不创建专用分支或 worktree。只有能够独立开发、独立验收或整体舍弃的重大功能才使用独立分支和 worktree；满足该条件时允许自行创建，无需事前批准。重大功能分支命名为 `NNN-<英文小写短横线名>`（如 `007-demo`），三位序号取现有分支与 `archive/` tag 的最大序号加一；从最新 `main` 创建，创建前确认分支名与 `archive/` tag 均未占用：
    ```powershell
    git worktree add -b 007-demo "D:\_Dev\ScratchEditor-worktrees\007-demo" main
    New-Item -ItemType Junction -Path "D:\_Dev\ScratchEditor-worktrees\007-demo\.tools" -Target "D:\_Dev\ScratchEditor\.tools"
    ```
12. **重大功能收尾批准、合并与部署**：重大功能开发和验证完成后，必须先向用户申请一次集中批准，明确列出归档 tag、非快进合并、安全移除 worktree、删除已合并分支和部署合并版本的整套收尾操作；获批后可连续完成当次已列明的流程，无需逐项重复申请，未获批准不得执行其中任何一步。批准后先在分支最终提交打轻量 tag `archive/NNN-<名>`（如 `git tag archive/007-demo 007-demo`），再在 `main` 上非快进合并，提交信息固定为 `merge: 合并<中文功能名>（NNN-<名>）`（如 `git merge --no-ff -m "merge: 合并示例功能（007-demo）" 007-demo`）；同时把已完结功能的需求文档移入 `docs/archive/` 并更新 `docs/README.md` 索引。集中批准不包含 `git push`，也不构成未来收尾操作的永久授权。
13. **分支删除与 tag 保留**：仅在规则 12 的当次集中批准范围内，且合并完成、`archive/` tag 已打后，先按规则 10 移除对应 worktree，再用 `git branch -d <分支名>` 删除分支；若 `-d` 拒绝删除，停止并向用户确认，禁止用 `-D` 强删。`archive/` tag 永久保留作为回溯依据。
14. **`.tools` 不被删除（结果导向硬性规则）**：除非有意，任何行为不得导致 `.tools` 被删除（目录或其中内容）。“有意”仅指为满足构建需要对 `.tools` 内文件进行删改（如 `scripts/restore-toolchain.ps1` 替换工具链），以及规则 10 中受控脚本对 worktree `.tools` 联接的解除；绝对禁止任何意外删除，包括对含共享联接的 worktree 执行递归删除、`git clean` 类清理、绕过脚本的 `git worktree remove`，以及其他可能沿 junction 波及共享工具链的操作。删除目标不明、`.tools` 状态异常或脚本拒绝执行时，停止并向用户确认，不得自行清理。
