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
- 主题配置以 `config/markdown-style.json` 为单一事实来源；窗口与快捷键配置在 `settings.ini`；测试使用隔离 INI。

## 必须遵守的规则

1. **不破坏用户环境**：除非用户明确要求部署，不得修改 AHK 文件、剪贴板、用户配置或稳定安装副本（`%LOCALAPPDATA%\ScratchEditor\CodexEditor`、`AhkEditor`）。
2. **开发迭代必须隔离**：使用 `editing`/`window-ui` preset 并传 `-SkipLocalInstall`。
3. **坐标单位**：一律使用 Qt UTF-16 code-unit 索引，不混用 UTF-8 字节偏移。
4. **性能约束**：编辑核心改动保持线性复杂度；每次编辑最多一次全文读取、一次分析；先收集插入点再倒序写入；禁止在候选循环中重复 `toPlainText()` 或全文扫描。
5. **测试纪律**：关键边界使用独立 check 名并输出 actual/expected 细节；先补能稳定失败的测试再实现；回归失败不得通过删除或放宽断言掩盖；偶发超时最多重跑一次并单独记录。
6. **提交、部署和文档维护**：重要改动完成后检查并更新 `docs/README.md` 索引；已完结的功能需求移入 `docs/archive/`；用户要求提交后，若最新版尚未部署，主动询问是否需要部署。
7. **文件编码**：项目内所有含中文的文本文件统一为 UTF-8（无 BOM）编码，不得混用 GBK/ANSI、UTF-16 或带 BOM 的 UTF-8；新增或编辑文件时保持该约定。
8. **沙箱权限**：开发沙箱对 `.git` 目录只读，`git add`/`git commit` 等写入 `.git` 的操作必须使用沙箱外权限（require_escalated）执行，可直接申请，无需先在沙箱内试错。
