# ScratchEditor

ScratchEditor 是从 AutoHotkey 临时编辑器迁移出的轻量 Windows 编辑器。主程序使用
Qt 6 Quick/QML、C++20 和 CMake；AutoHotkey 继续负责全局快捷键与启动调度，并通过
本地命名管道控制常驻的 `ScratchEditor.exe`。

迁移阶段 1–6 已完成。经用户明确批准并先创建同目录备份后，原
`D:\Documents\AutoHotkey\KeysRedirect.ahk` 已移除旧 GUI，只保留快捷键、Qt IPC
调度和启动失败时不改写内容的纯剪贴板回退。外部 AHK 文件和备份不属于本仓库。

## 功能

- 单实例常驻窗口，支持 `toggle`、`show`、`hide` 本地 IPC。
- 剪贴板载入/回写、Escape 关闭、焦点恢复和剪贴板异常保护。
- 无标题置顶窗口、四边缩放、窗口几何记忆、自动换行和智能滚动条。
- CJK 字体、微软拼音、高 DPI 和暗色首帧保障。
- 原生 Markdown 语法高亮与常用 Markdown 编辑命令。
- 查找替换、延迟加载命令面板和可配置快捷键。
- 延迟加载设置页、深浅主题、编辑字体/字号及轻量动画开关。
- 窗口、外观和快捷键统一保存在一个带 schema 的 INI 配置文件中。

按用户决定不提供 Markdown 预览。项目也不使用 Qt WebEngine、WebView 或其他浏览器
内核；历史草稿、标签页、固定草稿、多光标、插件和 LSP 当前均不在范围内。

## 架构与目录

```text
KeysRedirect.ahk ──命名管道──> ScratchEditor.exe
                                    ├─ C++：生命周期、IPC、剪贴板、配置、编辑命令
                                    └─ QML：编辑器、查找、命令面板、设置页与动画
```

- `src/`：C++20 应用与编辑核心。
- `qml/`：预编译 Qt Quick 界面。
- `integration/`：隔离的 AHK 迁移参考副本。
- `tests/`：C++ 验收程序和 AHK 测试夹具。
- `scripts/`：构建、功能回归和性能验收入口。
- `docs/`：分阶段计划与验收报告。
- `artifacts/baselines/`：纳入版本控制的阶段最终证据。

完整架构和阶段门槛见 [ScratchEditor-Migration.md](ScratchEditor-Migration.md)，文档索引见
[docs/README.md](docs/README.md)。

## 工具链与构建

已验证工具链：Qt 6.10.2、MinGW 13.1、CMake 3.25+、Ninja、Windows 11。
工作区工具链默认位于 `.tools/Qt`，该目录不会提交到 Git。

```powershell
./scripts/build.ps1
```

在不覆盖当前 `build/release` 实例的情况下验证当前源码，可使用：

```powershell
./scripts/build.ps1 -Preset stage4
```

也可以直接使用 CMake：

```powershell
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --preset release
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --build --preset release
```

`scripts/build.ps1` 会构建 `build/release` 并运行 `windeployqt`。如果该目录中的旧版本
正在运行，Windows 会锁定可执行文件；应先正常退出该实例，不能由构建脚本强制终止。
阶段 4 的已验证独立部署位于 `build/stage4/`。

## 运行与 IPC

```powershell
./build/release/ScratchEditor.exe --background
./build/release/ScratchEditor.exe --show
./build/release/ScratchEditor.exe --hide
./build/release/ScratchEditor.exe --toggle
```

常驻实例使用管道名 `ScratchEditor.Stage1.v1`，名称保持稳定是为了兼容既有 AHK 调度。
关闭窗口只会隐藏并复用进程，不会销毁编辑器。

正式配置存放在 Qt `AppConfigLocation` 下的 `settings.ini`。首次创建集中配置时会迁移
旧 Native Settings 中的窗口几何和快捷键；测试通过独立环境变量使用临时 INI。

## 验收

阶段 6 AHK 安装状态、备份、IPC、失败回退和进程保护验收：

```powershell
./scripts/run-stage6-tests.ps1
```

Qt 功能回归仍由阶段 4 入口覆盖阶段 3 和阶段 2。该历史入口的 AHK 基线参数应指向
阶段 6 备份：

```powershell
./scripts/run-stage4-tests.ps1 `
  -BuildSubdirectory build\stage4 `
  -OriginalAhkPath D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834
```

完整性能回归：

```powershell
./scripts/run-stage1-tests.ps1 `
  -BuildSubdirectory build\stage4 `
  -ServerName ScratchEditor.Validation.Perf `
  -ArtifactPrefix validation-performance
```

所有当前测试使用独立管道和测试配置，不会停止默认管道上的用户实例。详细说明见
[tests/README.md](tests/README.md)。

阶段 6 最终实测：冷启动最大 75.18 ms、热唤醒 P95 19.54 ms、10 万字输入到帧
P95 16.61 ms、空闲 CPU 0%、工作集 39.61 MB、动画 59.88 FPS；微软拼音精确提交
`你好`。完整 JSON 证据保存在 [artifacts/baselines](artifacts/baselines/README.md)。

## AHK 迁移边界

`integration/KeysRedirect.QtMigration.ahk` 是阶段 2 的历史隔离参考副本，仍保留当时的
Qt/旧 GUI 回退开关，不会被构建或测试脚本自动安装。阶段 6 当前状态：

- 原文件备份：`D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834`。
- 备份 SHA-256：`8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9`。
- 已安装文件 SHA-256：`EF7CCD4E2CDDB0D29F8790F116A0B319CA1F2925548F48A05A5B195ADFE7D823`。
- 默认 Qt 路径仍为 `D:\_Dev\ScratchEditor\build\stage4\ScratchEditor.exe`，未迁移任何
  现有文件位置。
- 验收未重载正在运行的 AHK；磁盘上的新版本会在用户下次正常重载或登录时生效。

Qt 部署资源目前会输出已知的 `libpng iCCP` 警告，不影响功能、像素检查或性能验收。
