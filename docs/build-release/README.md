# 构建与发行

在仓库根目录执行以下命令。日常开发使用隔离构建：

```powershell
./scripts/build.ps1 -Preset editing -SkipLocalInstall
```

测试程序、夹具和完整回归顺序见[测试指南](../../tests/README.md#推荐执行顺序)。

## 工具链与构建

已验证工具链：Qt 6.10.2、MinGW 13.1、CMake 3.25+、Ninja、Windows 11。
工作区工具链默认位于 `.tools/Qt`，该目录不会提交到 Git。

首次配置或 `.tools` 缓存丢失后，运行以下脚本即可从 Qt 官方仓库恢复项目锁定的完整工具链：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\restore-toolchain.ps1
```

脚本使用临时 Python 3.13 环境和固定版本的 `aqtinstall`，先在 `.tools` 下暂存并校验
Qt 6.10.2、MinGW 13.1、CMake 与 Ninja，成功后再替换 `.tools/Qt`。完整环境重复执行会直接跳过；
非空但不完整的目录默认拒绝覆盖，确认其中没有需要保留的内容后可显式传入 `-Force`。

创建 worktree 时若把其中的 `.tools` 设为指向主工作区 `.tools` 的目录联接，必须使用受保护的入口移除：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\remove-worktree.ps1 `
  -WorktreePath <worktree>
```

脚本会确认路径属于已注册的非主 worktree；若存在 `.tools`，则要求其必须是 `Junction`，只解除联接并
确认共享工具链仍完整后才执行 `git worktree remove`。不要绕过脚本直接移除仍包含共享 `.tools`
联接的 worktree，也不要对该联接递归删除；Git for Windows 可能沿联接清空主工作区工具链。

```powershell
./scripts/build.ps1
```

在不覆盖 `build/release` 的情况下进行隔离验证，可使用：

```powershell
./scripts/build.ps1 -Preset window-ui -SkipLocalInstall
```

隔离验证 preset 按职责命名：`editing` 覆盖 Markdown、高频编辑命令、查找替换与快捷键；
`window-ui` 覆盖设置、主题、窗口交互与动画。两组验证相互独立，完整回归时都应执行。

也可以直接使用 CMake：

```powershell
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --preset release
./.tools/Qt/Tools/CMake_64/bin/cmake.exe --build --preset release
```

`scripts/build.ps1` 会构建所选 preset、运行 `windeployqt`，并自动把刚构建的主程序同步安装到
`%LOCALAPPDATA%\ScratchEditor\CodexEditor` 与 `%LOCALAPPDATA%\ScratchEditor\AhkEditor`。前者由
Codex 和 pi 共用，后者供 AHK 常驻实例使用。构建脚本会在更新 AHK 副本前先核对运行路径，再通过专用
IPC 命令让稳定常驻实例自行退出，并在安装后重新启动；这也兼容 AHK 启动的高权限进程。只有明确传入
`-SkipLocalInstall` 才会跳过这两个本机副本；直接运行 CMake 也不会执行本机同步。

如需检查开发者本机集成状态而不重新安装，可运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1 -Action Check
```

## 在稳定版与临时测试版之间切换 AHK 快捷键

AHK 始终向固定生产管道发送 Scroll Lock、Win+F 等快捷键命令，因此无需修改或重载项目外的
`KeysRedirect.ahk`。仓库内切换器会先显示当前常驻实例，再正常保存并退出旧实例，最后让目标版本
接管同一管道：

```powershell
# 只查看当前状态，不切换
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 -StatusOnly

# 只列出当前工作区和所有已注册 worktree 中的可用构建
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 -ListCandidates

# 首次明确选择测试产物；相对路径按项目根目录解析
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 `
  -TestEditorPath .\build\editing\ScratchEditor.exe

# 之后无参数运行：测试版切回稳定版，稳定版切到交互选择的测试产物
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1
```

也可直接双击项目根目录的 `switch-ahk-editor.ps1.lnk` 执行同一切换流程。

当前无实例时，脚本会引导选择稳定版或测试版；切到测试版但未指定路径时，会扫描当前工作区以及
`git worktree list --porcelain` 返回的全部已注册 worktree，并按修改时间列出各自直接位于
`build/*/ScratchEditor.exe` 的候选。扫描不递归进入构建目录，并会跳过 worktree、`build` 或构建
子目录上的 reparse point。输出统一使用中文，其中 `[当前]`、`[目标]`、`[停止]`、`[启动]`、
`[已启用]`、`[警告]` 和 `[错误]` 等标志使用不同终端颜色，持续说明所处状态、PID 与完整路径。
目标会先完成存在性校验，启动后还会通过 IPC 复核 PID、可执行文件路径和 ready 状态；启动失败会
清理失败进程并尝试恢复刚才的版本。中文文案由 `config/switch-ahk-editor.zh-CN.json` 集中维护，
操作成功时会在结束提示后保留 2 秒缓冲，避免快捷方式窗口立即关闭；操作失败时窗口不会自动关闭，
只能由用户手动关闭，以便完整查看错误与恢复提示。
脚本显式以 UTF-8 读取，以兼容 Windows PowerShell 5.1 和项目 UTF-8 无 BOM 约束。

若当前常驻实例权限高于调用终端，脚本会显示 `access denied` 并保持现状；从管理员 PowerShell
重新运行同一命令即可。管道存在但无法识别实例时同样安全失败，不会误判为“当前无实例”。

切换使用现有 `shutdownForUpdate` 正常关闭语义：若窗口可见，当前草稿会先按普通关闭行为写回
剪贴板。正常退出超时后，脚本只会在重新核验旧实例 PID、启动时间和路径后询问是否强制终止；
非交互环境不会强制终止任何进程。测试版以普通常驻模式运行并共享正式设置、主题、剪贴板历史
和系统剪贴板，不使用 `--test-mode`，也不会覆盖 `%LOCALAPPDATA%\ScratchEditor\AhkEditor` 稳定副本。

切换器的自动验收使用唯一测试管道、内存剪贴板和临时 INI，不会连接或停止真实常驻实例：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-editor-switch-tests.ps1
```

## 公开发行流程

ScratchEditor 的公开 Windows x64 发行通过 GitHub Releases 提供便携 ZIP，并为 WinGet 社区仓库
生成多文件清单。当前公开版本基线为 `0.1.0`，标签形式为 `v0.1.0`。

### 自动构建

将 `v<版本>` 标签推送到 GitHub 后，`.github/workflows/release.yml` 在 `windows-2025` x64 runner 上：

1. 核对标签版本与 `CMakeLists.txt` 中的项目版本一致。
2. 使用 `scripts/restore-toolchain.ps1` 恢复 Qt 6.10.2、MinGW 13.1 及项目工具目录中的 CMake、Ninja。
3. 使用 `scripts/build.ps1 -Preset release -SkipLocalInstall` 构建，不安装稳定副本、不刷新本机集成。
4. 运行外部文件、剪贴板历史、编辑、窗口 UI 隔离测试。
5. 在唯一的系统临时目录重新部署 `ScratchEditor.exe` 的运行时依赖，只打包应用、默认配置和发行许可材料。
6. 解压发行 ZIP 到临时目录并清空子进程的 PATH 后，运行外部编辑器进程冒烟验收。
7. 所有步骤通过后，使用最小 `contents: write` 工作流权限创建草稿发行版并附带 ZIP、SHA-256 文件和 WinGet 清单 ZIP。

发行 ZIP 名称为 `ScratchEditor-<版本>-windows-x64.zip`。包根目录只有运行应用所需文件、`config/`、
`licenses/`、`README.txt` 和 `THIRD-PARTY-NOTICES.txt`，不含测试二进制或开发工具。打包脚本拒绝版本号
与 CMake 项目版本不一致的调用。

### 草稿检查与发布

发布前必须在未安装 Qt 开发工具链的 Windows 11 x64 环境验证 ZIP 启动及 `--wait` 保存；
托管的 `windows-2025` runner 与本机隔离 PATH 冒烟不能替代这一验收。

维护者应在 GitHub Releases 中检查发行说明、ZIP、校验文件、WinGet 清单以及 Actions 验收结果，确认后
手动发布草稿。首发未签名；发行说明明确提示 Windows 可能显示未知发布者。SHA-256 文件由本地包生成，
可通过 PowerShell `Get-FileHash -Algorithm SHA256` 复核。

### WinGet 清单

自动构建生成 `leixin0725.ScratchEditor` 的 version、defaultLocale 和 installer 三份清单，压缩为
`ScratchEditor-<版本>-winget-manifest.zip`。清单使用 ZIP 内的 `ScratchEditor.exe` 作为 portable 安装器，
发行 ZIP URL 和 SHA-256 根据本次实际文件写入。允许 WinGet 管理命令入口及所需用户 PATH；
应用和发行脚本不修改外部工具配置。

正式发布 GitHub Release 后，维护者应：

1. 解压 WinGet 清单 ZIP，并确认包标识、版本、GitHub 下载地址、SHA-256 和内嵌文件路径。
2. 使用 `winget validate --manifest <清单目录>` 校验；在干净 Windows 环境验证安装、升级和卸载。
3. 确认卸载只移除 WinGet 管理的包入口，不移除 `%LOCALAPPDATA%\ScratchEditor\ScratchEditor` 中用户数据。
4. 将该版本清单手动提交到 `microsoft/winget-pkgs`，处理仓库验证反馈。

不使用跨仓库 token，也不由本仓库工作流自动提交 WinGet PR。

### 许可与依赖核对

发行前检查 ZIP 中的 `THIRD-PARTY-NOTICES.txt`、`licenses/` 和 Qt SBOM，确保内容与实际部署目录中的 DLL、
插件及 QML 模块相符。`packaging/licenses/` 保存 Qt 6.10.2 上游许可正文，包括 LGPLv3、其引用的 GPLv3
及三个部署模块的许可文本；打包缺少这些材料会失败。发行包还附带 ScratchEditor MIT、Lucide/Feather、
MinGW 运行时许可和 Qt SBOM 中的组件版权、来源及自定义许可正文。许可材料来源记录见
`packaging/licenses/SOURCES.md`。手动打包可通过 `-QtLicenseFile <路径>` 覆盖 LGPL 正文。
`.gitattributes` 将 `packaging/licenses/**` 固定为 LF，防止 Windows checkout 转换为 CRLF 后
导致原始字节 SHA-256 与上游快照不一致；打包始终保留原始字节完整性校验。
更新 Qt/MinGW 版本或部署模块时，必须重新核对组件清单和所适用许可。

### 本地验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release -SkipLocalInstall
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-release.ps1 -Version 0.1.0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-release-package.ps1 `
  -PackagePath artifacts\release\ScratchEditor-0.1.0-windows-x64.zip
```
