# 2026-08-05 窗口放置与外部提示词编辑器更新

## 范围

本次更新围绕窗口放置、多屏热插拔与外部提示词编辑器体验，包含：

- 临时编辑器与外部提示词编辑器独立的几何记忆；
- 外部提示词编辑器只记忆尺寸，每次唤起就近摆放并避让临时编辑器；
- 多屏热插拔（拔/插主屏或副屏）后窗口位置、尺寸与渲染一致性的修复；
- 外部文件被清理（唤起它的终端已关闭）后静默退出；
- 外部提示词编辑器标题标注调用它的 CLI 类型。

## 最终行为

- 临时编辑器：每次打开回到上次关闭位置（进程内记忆），记忆失效时在焦点附近唤出。
- 外部提示词编辑器：只记忆窗口大小；每次唤起优先完整落在单个屏幕内，按
  记忆尺寸 → 默认尺寸 → 最小尺寸的阶梯摆放，靠近唤起终端窗口，并尽量避开
  已打开的临时编辑器窗口。
- 热插拔：
  - 拔掉任一屏幕后，窗口保持逻辑尺寸、DPR 与渲染缓冲一致（无边框截断）；
  - 屏幕恢复后，窗口回到拔屏前所在的屏幕（主屏被拔则回主屏，副屏被拔则回副屏）。
- 外部文件已被清理时，`Ctrl+S` / `Esc` 静默退出（退出码 0），不保存、不显示错误。
- 外部提示词编辑器标题显示 CLI 类型（`Codex` / `pi` / `Claude Code` 等；
  无法识别时不附加后缀）。

## 根因

- Qt 在“主屏被拔除”时不像非主屏那样主动迁移窗口，QWindow 的屏幕/DPR 缓存会
  失效，`geometry()` 按错误的缩放系数换算，导致渲染缓冲与窗口外框尺寸错位
  （右/下边框被截断或边框远大于缓冲）。
- 恢复双屏时，Qt/Windows 的坐标空间会短暂重叠（副屏仍报告原点 0、主屏已重新
  加入），窗口会被错误关联到重新加入的屏幕；单次位置校正还会被后续系统迁移
  事件覆盖，导致窗口不回原屏幕。
- 副屏被拔除时 Qt 会先把窗口屏幕改到主屏，按 `window->screen()` 判断会漏掉
  锚点快照时机。
- 外部编辑器保存目标不存在时，原逻辑把“找不到路径”当作错误并保持窗口打开，
  用户无法关闭窗口。

## 修复方案

- 放置算法抽到 `WindowPlacement`（`fitRestoredGeometry` / `placeNearWindow` /
  `nativeToLogicalRect`），并补充混合 DPI、尺寸阶梯、锚定顺序与重叠避让的
  单元测试。
- 独立几何记忆：设置新增 `window/externalGeometry`；生产 IPC 新增只读
  `getWindowGeometry`，供外部进程查询常驻临时编辑器几何以避让。
- 外部模式使用进程启动时的前台窗口快照作为唤起者，避免 Windows
  Terminal/ConPTY 祖先链不可靠的问题。
- 热插拔：
  - `WM_DISPLAYCHANGE` 原生事件过滤器第一时间冻结锚点更新（2 秒），期间
    Qt/Windows 的迁移事件不再改写“原屏幕记忆”；
  - `Move` / `screenChanged` 实时跟踪窗口所在屏幕与屏内偏移；
  - 屏幕移除时用“窗口原生矩形 ∩ 被移除屏幕物理矩形”快照锚点；
  - 校正时优先用原生 `GetWindowRect` 反推真实逻辑位置，并保留记忆尺寸；
  - 屏幕几何重叠时延迟校正（最多重试 20 次）；
  - 用原生坐标 `SetWindowPos`（+1px 后立即还原）触发真实
    `WM_DPICHANGED`/`WM_SIZE`，并在 0/600/1400ms 三次重复断言直到稳定。
- 外部文件缺失：保存前检查文件/目录存在性，缺失则静默退出且不保存。
- CLI 类型：读取外部编辑进程的父进程 exe 名（codex / pi / claude /
  node+CODEX_HOME）映射为标题后缀。

## 验证

- window-ui：全部通过（含放置算法单元校验、20 轮动画稳定性、几何持久化、
  `getWindowGeometry` 查询）。
- editing：全部通过。
- external-file：全部通过。
- external-process：仅“Ctrl+S 真实输入”因自动化桌面无法抢到前台焦点而超时
  （环境限制，与本次改动无关）。
- 本机双屏复现（主屏 125%、副屏 200%）：分别拔主屏/副屏再恢复，窗口均回到
  原屏幕，尺寸保持，边框正常；三次收敛校验 actual == expected。
- `git diff --check`：通过。

## 涉及文件

- `src/windowplacement.{h,cpp}`（新增）
- `src/editorcontroller.{h,cpp}`
- `src/appsettings.{h,cpp}`
- `qml/Main.qml`
- `tests/window_ui_main.cpp`、`tests/externaleditorprocess_main.cpp`
- `README.md`、`tests/README.md`、`docs/README.md`

## 部署

稳定副本（CodexEditor / AhkEditor）已更新，SHA-256 `90CF1B71...`。
