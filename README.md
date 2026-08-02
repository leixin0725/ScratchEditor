# ScratchEditor

ScratchEditor 是从 AutoHotkey 临时编辑器迁移出的轻量 Windows 编辑器。主程序使用
Qt 6 Quick/QML、C++20 和 CMake；AutoHotkey 继续负责全局快捷键与启动调度，并通过
本地命名管道控制常驻的 `ScratchEditor.exe`。

迁移阶段 1–5 已完成。阶段 6（从原 `KeysRedirect.ahk` 移除旧 GUI）尚未执行，必须
等待明确批准。原始 AHK 文件不属于本仓库，所有构建与测试都不会修改它。

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

当前功能回归（阶段 4，同时覆盖阶段 3 和阶段 2）：

```powershell
./scripts/run-stage4-tests.ps1 -OriginalAhkPath D:\Documents\AutoHotkey\KeysRedirect.ahk
```

完整性能回归：

```powershell
./scripts/run-stage1-tests.ps1 `
  -BuildSubdirectory build\stage4 `
  -ServerName ScratchEditor.Validation.Perf `
  -ArtifactPrefix validation-performance
```

AHK 持久 IPC 隔离测试：

```powershell
./scripts/test-ahk-ipc.ps1 `
  -BuildSubdirectory build\stage4 `
  -OriginalAhkPath D:\Documents\AutoHotkey\KeysRedirect.ahk
```

也可设置 `SCRATCHEDITOR_ORIGINAL_AHK`，避免在命令行重复提供路径。所有当前测试使用
独立管道和测试配置，不会停止默认管道上的用户实例。详细说明见
[tests/README.md](tests/README.md)。

阶段 4 最终实测：冷启动最大 82.84 ms、热唤醒 P95 25.96 ms、10 万字输入到帧
P95 16.15 ms、空闲 CPU 0%、工作集 39.50 MB、动画 60.16 FPS；微软拼音精确提交
`你好`。完整 JSON 证据保存在 [artifacts/baselines](artifacts/baselines/README.md)。

## AHK 迁移边界

`integration/KeysRedirect.QtMigration.ahk` 是隔离参考副本，包含 Qt/旧 GUI 回退开关；
它不会被自动复制或覆盖到用户的 AHK 仓库。阶段 6 获批前：

- 保留原 AHK 编辑器模块和回退路径。
- 不改写原 `KeysRedirect.ahk`。
- 不停止用户当前运行的 AHK 或 ScratchEditor 实例。

Qt 部署资源目前会输出已知的 `libpng iCCP` 警告，不影响功能、像素检查或性能验收。
