#Requires AutoHotkey v2.0
#SingleInstance Force

; ==============================================================================
; AutoHotkey 多功能脚本
; ==============================================================================
; 模块索引
; 0. 启动保护：统一输入层级，强制关闭 CapsLock 锁定状态
; 1. 基础按键：CapsLock 短按 Esc、按住 Ctrl；ScrollLock/Pause 映射到 F14/F15
; 2. Copilot 键：拦截 F23/Win+Shift+F23，映射为右 Ctrl，并修正 Win/Shift 卡键
; 3. Obsidian：PrintScreen/F13 呼出、隐藏、启动，并记住切回窗口
; 4. Git Bash：Ctrl+Shift+Insert 呼出；激活时批量最小化所有 Git Bash 窗口
; 5. Chrome：Ctrl+Alt+G 呼出、隐藏、启动；从普通权限 Shell 启动 Chrome
; 6. 临时编辑器：Ctrl+Alt+V 用剪贴板内容打开置顶草稿窗口
; ==============================================================================

; ==============================================================================
; 0. 启动保护与全局输入层级
; ==============================================================================

global LastActiveWindow := 0
global IsTogglingObsidian := false
; 阶段 2 回退开关：false 立即回到下方保留的原 AHK 临时编辑器。
global UseQtScratchEditor := true
global QtScratchEditorPipe := "\\.\pipe\ScratchEditor.Stage1.v1"
global QtScratchEditorExe := A_ScriptDir "\..\build\release\ScratchEditor.exe"
global QtScratchEditorPipeHandle := -1

; 隔离副本专用验证入口：在注册业务热键前测试持久 IPC，然后立即退出。
if A_Args.Length >= 1 && A_Args[1] = "--migration-ipc-test" {
    shown := EnsureQtScratchEditorAndSend("show")
    Sleep 300
    hidden := EnsureQtScratchEditorAndSend("hide")
    FileAppend(shown && hidden ? "qt-ipc-pass`n" : "qt-ipc-fail`n", "*")
    ExitApp(shown && hidden ? 0 : 1)
}

#InputLevel 1
SendLevel 1

; CapsLock 在本脚本中被用作功能键，不允许保留大写锁定状态。
if GetKeyState("CapsLock", "T") {
    SetCapsLockState "Off"
}
SetCapsLockState "AlwaysOff"

; 某些远程桌面、虚拟机或高权限程序可能重置 CapsLock 状态，这里定期拉回。
SetTimer EnforceCapsLockOff, 2000
SetTimer StartQtScratchEditorResident, -1
EnforceCapsLockOff() {
    if GetKeyState("CapsLock", "T") {
        SetCapsLockState "AlwaysOff"
    }
}

; ==============================================================================
; 1. 基础按键映射
; ==============================================================================

; CapsLock 策略
; - 短按并松开：Esc
; - 按住并配合非修饰键：左 Ctrl，例如 CapsLock+C => Ctrl+C
; - 按住并配合修饰键短按：保留修饰键发送 Esc，例如 Ctrl+Shift+CapsLock => Ctrl+Shift+Esc
; - Alt 先于 CapsLock 松开：补偿真实 Esc 的 Alt+Esc 行为
; - 长按后松开：只释放 Ctrl，不发送 Esc
global CapsAsCtrlDown := false
global CapsAsCtrlPressedAt := 0
global CapsTapHadNonModifier := false
global CapsTapEscSent := false
global CapsTapStartedWithAlt := false
global CapsTapInput := 0
global CapsTapEscThresholdMs := 180

*CapsLock:: {
    global CapsAsCtrlDown, CapsAsCtrlPressedAt, CapsTapHadNonModifier, CapsTapEscSent, CapsTapStartedWithAlt, CapsTapInput
    Critical
    SetKeyDelay -1

    ; 忽略键盘固件或驱动产生的 CapsLock 自动重复 Down 事件。
    if CapsAsCtrlDown
        return

    CapsAsCtrlDown := true
    CapsAsCtrlPressedAt := A_TickCount
    CapsTapHadNonModifier := false
    CapsTapEscSent := false
    CapsTapStartedWithAlt := (GetKeyState("LAlt", "P") || GetKeyState("RAlt", "P"))

    ; 用 InputHook 观察 CapsLock 按住期间是否按过非修饰键。
    CapsTapInput := InputHook("V")
    CapsTapInput.KeyOpt("{All}", "N")
    CapsTapInput.OnKeyDown := CapsAsCtrlOnKeyDown
    CapsTapInput.Start()

    Send "{Blind}{LCtrl DownR}"
}

*CapsLock Up:: {
    global CapsAsCtrlDown, CapsAsCtrlPressedAt, CapsTapHadNonModifier, CapsTapEscSent, CapsTapStartedWithAlt, CapsTapInput, CapsTapEscThresholdMs
    Critical
    SetKeyDelay -1

    if !CapsAsCtrlDown
        return

    if CapsTapInput {
        CapsTapInput.Stop()
        CapsTapInput := 0
    }

    heldMs := A_TickCount - CapsAsCtrlPressedAt
    ; 触发 Esc 的条件保持保守：短按、未按非修饰键、最后一个键仍是 CapsLock 或修饰键。
    shouldSendEsc := (
        heldMs <= CapsTapEscThresholdMs
        && !CapsTapEscSent
        && !CapsTapHadNonModifier
        && CapsAsCtrlAllowsTapPriorKey(A_PriorKey)
    )

    if shouldSendEsc {
        if (GetKeyState("LCtrl", "P") || GetKeyState("RCtrl", "P")) {
            SendEscCompat()
            CapsAsCtrlRelease()
        } else {
            CapsAsCtrlRelease()
            SendEscCompat()
        }
    } else {
        CapsAsCtrlRelease()
    }

    CapsAsCtrlDown := false
    CapsAsCtrlPressedAt := 0
    CapsTapHadNonModifier := false
    CapsTapEscSent := false
    CapsTapStartedWithAlt := false
}

~*LAlt Up::
~*RAlt Up:: {
    Critical
    SetKeyDelay -1
    CapsAsCtrlSendAltEscOnAltUp()
}

CapsAsCtrlOnKeyDown(inputHook, vk, sc) {
    global CapsAsCtrlDown, CapsTapHadNonModifier

    if (CapsAsCtrlDown && !CapsAsCtrlIsModifierVk(vk))
        CapsTapHadNonModifier := true
}

CapsAsCtrlRelease() {
    lctrlPhysicallyDown := GetKeyState("LCtrl", "P")

    Send "{Blind}{LCtrl Up}"

    ; 如果用户真实按住了左 Ctrl，释放 CapsLock 的虚拟 Ctrl 后恢复其逻辑按下状态。
    if lctrlPhysicallyDown
        Send "{Blind}{LCtrl Down}"
}

SendEscCompat() {
    ; ppInk polls key state every 15 ms, so an instantaneous tap can be missed.
    SendEvent "{Blind}{Esc Down}"
    Sleep 45
    SendEvent "{Blind}{Esc Up}"
}

CapsAsCtrlSendAltEscOnAltUp() {
    global CapsAsCtrlDown, CapsAsCtrlPressedAt, CapsTapHadNonModifier, CapsTapEscSent, CapsTapStartedWithAlt, CapsTapEscThresholdMs

    if (!CapsAsCtrlDown || !CapsTapStartedWithAlt || CapsTapEscSent || CapsTapHadNonModifier)
        return

    if (A_TickCount - CapsAsCtrlPressedAt > CapsTapEscThresholdMs)
        return

    ; Alt+Esc 需要在没有 CapsLock 虚拟 Ctrl 干扰的情况下发送。
    Send "{Blind}{LCtrl Up}"
    Send "{Blind}{Alt Down}{Esc}{Alt Up}"
    Send "{Blind}{LCtrl DownR}"

    CapsTapEscSent := true
}

CapsAsCtrlAllowsTapPriorKey(keyName) {
    return (keyName = "CapsLock" || CapsAsCtrlIsModifierName(keyName))
}

CapsAsCtrlIsModifierVk(vk) {
    switch vk {
        case 0x10, 0x11, 0x12, 0x14, 0x5B, 0x5C, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5:
            return true
    }

    return false
}

CapsAsCtrlIsModifierName(keyName) {
    switch keyName {
        case "Shift", "LShift", "RShift",
             "Ctrl", "Control", "LCtrl", "RCtrl", "LControl", "RControl",
             "Alt", "LAlt", "RAlt",
             "LWin", "RWin":
            return true
    }

    return false
}

ScrollLock::F14
Pause::F15

; ==============================================================================
; 2. Copilot 键映射：F23 / Win+Shift+F23 => 右 Ctrl
; ==============================================================================

global CopilotAsCtrlDown := false

; 部分键盘的 Copilot 键会发送 Win+Shift+F23；这里同时拦截裸 F23 和组合形态。
<#<+F23::
*F23:: {
    global CopilotAsCtrlDown
    Critical
    SetKeyDelay -1

    ; 发送无作用虚拟键，打断系统对 Win 键的菜单触发判断。
    Send "{Blind}{vkE8}"

    ; 释放硬件带来的 Win+Shift，再把 Copilot 的按住状态映射为右 Ctrl。
    Send "{Blind}{LWin Up}{LShift Up}"
    if !CopilotAsCtrlDown {
        CopilotAsCtrlDown := true
        Send "{Blind}{RCtrl DownR}"
    }
}

<#<+F23 Up::
*F23 Up:: {
    global CopilotAsCtrlDown
    Critical
    SetKeyDelay -1

    if CopilotAsCtrlDown {
        Send "{Blind}{RCtrl Up}"
        CopilotAsCtrlDown := false
    }

    ; 再次防御性释放，避免硬件释放信号慢半拍造成卡 Win/Shift。
    Send "{Blind}{LWin Up}{LShift Up}"
}

; ==============================================================================
; 3. Obsidian 呼出 / 隐藏 / 启动：PrintScreen 或 F13
; ==============================================================================

global LastToggleTime := 0

PrintScreen::
F13:: {
    global LastActiveWindow, LastToggleTime

    ; 时间戳防抖：过滤 200ms 内的重复触发。
    if (A_TickCount - LastToggleTime < 200)
        return
    LastToggleTime := A_TickCount

    ; 记录当前焦点窗口，隐藏 Obsidian 时切回来。
    CurrentActive := WinActive("A")

    if WinExist("ahk_exe Obsidian.exe") {
        if WinActive("ahk_exe Obsidian.exe") {
            WinMinimize("ahk_exe Obsidian.exe")
            if (LastActiveWindow and WinExist("ahk_id " LastActiveWindow)) {
                WinActivate("ahk_id " LastActiveWindow)
            }
        } else {
            LastActiveWindow := CurrentActive
            WinActivate("ahk_exe Obsidian.exe")
        }
    } else {
        LastActiveWindow := CurrentActive
        Run("D:\ObsidianLoader\Obsidian.vbs", "D:\ObsidianLoader")
    }
}

; ==============================================================================
; 4. Git Bash 呼出 / 批量隐藏：Ctrl+Shift+Insert
; ==============================================================================

global GitBashLastActiveWindow := 0
global GitBashLastToggleTime := 0

^+Insert:: {
    global GitBashLastActiveWindow, GitBashLastToggleTime

    if (A_TickCount - GitBashLastToggleTime < 200)
        return
    GitBashLastToggleTime := A_TickCount

    CurrentActive := WinActive("A")

    ; 当前在 Git Bash 时，隐藏所有 mintty 窗口并切回原窗口。
    if WinActive("ahk_exe mintty.exe") {
        bashList := WinGetList("ahk_exe mintty.exe")

        for index, hwnd in bashList {
            WinMinimize("ahk_id " hwnd)
        }

        if (GitBashLastActiveWindow and WinExist("ahk_id " GitBashLastActiveWindow)) {
            WinActivate("ahk_id " GitBashLastActiveWindow)
        }
    }
    ; 后台已有 Git Bash 时，激活最近使用的窗口。
    else if WinExist("ahk_exe mintty.exe") {
        GitBashLastActiveWindow := CurrentActive
        WinActivate("ahk_exe mintty.exe")
    }
    ; 没有 Git Bash 时，从固定工作目录启动。
    else {
        GitBashLastActiveWindow := CurrentActive
        Run("D:\Git\git-bash.exe","D:\LEIXIN2025\Notes")
    }
}

; ==============================================================================
; 5. Chrome 呼出 / 隐藏 / 启动：Ctrl+Alt+G
; ==============================================================================

; 从管理员身份运行的 AHK 里直接 Run Chrome，会让 Chrome 继承管理员权限。
; 这里借用桌面 Explorer 的 ShellExecute，让 Chrome 从普通权限 Shell 启动。
RunUnelevated(target, params := "", workingDir := "") {
    static VT_UI4 := 0x13
    static SWC_DESKTOP := ComValue(VT_UI4, 0x8)

    desktopShell := ComObject("Shell.Application").Windows.Item(SWC_DESKTOP).Document.Application
    desktopShell.ShellExecute(target, params, workingDir, "open", 1)
}

GetChromePath() {
    chromePaths := []

    ; AHK v2 没有 A_LocalAppData，使用环境变量兼容 #Warn。
    localAppData := EnvGet("LOCALAPPDATA")
    if (localAppData != "")
        chromePaths.Push(localAppData "\Google\Chrome\Application\chrome.exe")

    chromePaths.Push(A_ProgramFiles "\Google\Chrome\Application\chrome.exe")

    pf86 := EnvGet("ProgramFiles(x86)")
    if (pf86 != "")
        chromePaths.Push(pf86 "\Google\Chrome\Application\chrome.exe")

    for chromePath in chromePaths {
        if FileExist(chromePath)
            return chromePath
    }

    ; 兜底交给 Shell：PATH / App Paths 注册表项都可能命中。
    return "chrome.exe"
}

global ChromeLastActiveWindow := 0
global ChromeLastToggleTime := 0

!^g:: {
    global ChromeLastActiveWindow, ChromeLastToggleTime

    ; 时间戳防抖：过滤 200ms 内的重复触发。
    if (A_TickCount - ChromeLastToggleTime < 200)
        return
    ChromeLastToggleTime := A_TickCount

    CurrentActive := WinActive("A")

    if WinExist("ahk_exe chrome.exe") {
        if WinActive("ahk_exe chrome.exe") {
            WinMinimize("ahk_exe chrome.exe")
            if (ChromeLastActiveWindow and WinExist("ahk_id " ChromeLastActiveWindow)) {
                WinActivate("ahk_id " ChromeLastActiveWindow)
            }
        } else {
            ChromeLastActiveWindow := CurrentActive
            WinActivate("ahk_exe chrome.exe")
        }
    } else {
        ChromeLastActiveWindow := CurrentActive

        chromePath := GetChromePath()
        chromeWorkDir := ""
        if (InStr(chromePath, "\"))
            chromeWorkDir := RegExReplace(chromePath, "\\[^\\]+$")

        RunUnelevated(chromePath, "", chromeWorkDir)

        ; 降权启动经由 Explorer，窗口前台激活不稳定；新窗口出现后主动激活一次。
        chromeHwnd := WinWait("ahk_exe chrome.exe", , 5)
        if (chromeHwnd) {
            WinRestore("ahk_id " chromeHwnd)
            WinActivate("ahk_id " chromeHwnd)
        }
    }
}

; ==============================================================================
; 6. 临时编辑器：F14
; ==============================================================================

; 行为：未打开时用剪贴板内容创建置顶草稿窗口；关闭时自动把全部内容写回剪贴板。

global ScratchGui := ""
global ScratchVisible := false
OnExit(CloseScratchOnExit)
OnExit((*) => CloseQtScratchEditorPipe())

; 窗口与编辑区参数。

global WinW := 920
global WinH := 640

global DragZoneH := 52        ; 顶部可拖动区域高度
global Margin := 18           ; 编辑区左右和底部边距
global EdgeDragW := 14        ; 四边与四角的缩放命中宽度
global EditTopDragExtra := 18 ; Edit 控件内部顶部额外可拖动高度

global BgColor := "252525"
global BarColor := "202020"
global TextColor := "F2F2F2"
global TitleColor := "FFFFFF"
global HintColor := "9A9A9A"

global UiFont := "Microsoft YaHei UI"
global EditFontSize := 13
global EditFont := "Microsoft YaHei UI"
global HeaderHintW := 150
global EditPaddingX := 12

; 顶部区域用于移动窗口，四边与四角使用 Windows 原生缩放逻辑。
OnMessage(0x0201, TryNativeDrag) ; WM_LBUTTONDOWN
OnMessage(0x0203, TryNativeDrag) ; WM_LBUTTONDBLCLK
OnMessage(0x0084, ScratchHitTest) ; WM_NCHITTEST

ScrollLock::ToggleScratchWithMigration()


ToggleScratchWithMigration() {
    global UseQtScratchEditor

    if UseQtScratchEditor && EnsureQtScratchEditorAndSend("toggle") {
        return
    }
    ToggleScratch()
}


StartQtScratchEditorResident() {
    global UseQtScratchEditor

    if UseQtScratchEditor {
        EnsureQtScratchEditorResident()
    }
}


EnsureQtScratchEditorResident() {
    global QtScratchEditorExe, QtScratchEditorPipe

    if SendQtScratchEditorCommand("ping") {
        return true
    }
    if !FileExist(QtScratchEditorExe) {
        return false
    }

    try {
        Run('"' QtScratchEditorExe '" --background')
    } catch {
        return false
    }
    return DllCall("WaitNamedPipeW", "Str", QtScratchEditorPipe, "UInt", 1500)
}


EnsureQtScratchEditorAndSend(command) {
    if SendQtScratchEditorCommand(command) {
        return true
    }
    if !EnsureQtScratchEditorResident() {
        return false
    }
    return SendQtScratchEditorCommand(command)
}


SendQtScratchEditorCommand(command) {
    global QtScratchEditorPipe, QtScratchEditorPipeHandle

    if QtScratchEditorPipeHandle = -1 {
        QtScratchEditorPipeHandle := DllCall(
            "CreateFileW",
            "Str", QtScratchEditorPipe,
            "UInt", 0x40000000, ; GENERIC_WRITE
            "UInt", 0,
            "Ptr", 0,
            "UInt", 3,          ; OPEN_EXISTING
            "UInt", 0,
            "Ptr", 0,
            "Ptr"
        )
    }
    if QtScratchEditorPipeHandle = -1 {
        return false
    }

    payloadChars := StrPut(command "`n", "UTF-8")
    payload := Buffer(payloadChars)
    payloadBytes := StrPut(command "`n", payload, "UTF-8") - 1
    bytesWritten := 0
    ok := DllCall(
        "WriteFile",
        "Ptr", QtScratchEditorPipeHandle,
        "Ptr", payload,
        "UInt", payloadBytes,
        "UInt*", &bytesWritten,
        "Ptr", 0
    )
    if !ok || bytesWritten != payloadBytes {
        CloseQtScratchEditorPipe()
        return false
    }
    return true
}


CloseQtScratchEditorPipe() {
    global QtScratchEditorPipeHandle

    if QtScratchEditorPipeHandle != -1 {
        DllCall("CloseHandle", "Ptr", QtScratchEditorPipeHandle)
        QtScratchEditorPipeHandle := -1
    }
}


ToggleScratch() {
    global ScratchGui, ScratchVisible
    global WinW, WinH, DragZoneH, Margin
    global BgColor, BarColor, TextColor, TitleColor, HintColor
    global UiFont, EditFontSize, EditFont, HeaderHintW, EditPaddingX

    if IsObject(ScratchGui) {
        if ScratchVisible {
            CloseScratch()
        } else {
            ShowExistingScratch()
        }
        return
    }

    ; 无标题栏、置顶、工具窗口：作为轻量临时草稿使用。
    ScratchGui := Gui("-Caption +ToolWindow +AlwaysOnTop +Resize +MinSize500x320", "Scratch")
    ScratchGui.BackColor := BarColor

    editX := Margin
    editY := DragZoneH
    editW := WinW - Margin * 2
    editH := WinH - DragZoneH - Margin

    ScratchGui.SetFont("s11 w600 c" TitleColor, UiFont)
    ScratchGui.Add(
        "Text",
        "vScratchTitle x" Margin " y14 h24 BackgroundTrans +0x200",
        "临时编辑器"
    )

    ScratchGui.SetFont("s9 w400 c" HintColor, UiFont)
    ScratchGui.Add(
        "Text",
        "vScratchHint x" (WinW - Margin - HeaderHintW) " y15 "
            . "w" HeaderHintW " h24 Right BackgroundTrans +0x200",
        "Esc 关闭并复制"
    )

    ScratchGui.SetFont("s" EditFontSize " c" TextColor, EditFont)

    editOptions := "vScratchText "
        . "x" editX " y" editY " "
        . "w" editW " h" editH " "
        . "Multi WantTab +Wrap +VScroll -E0x200 "
        . "Background" BgColor " c" TextColor

    edit := ScratchGui.Add("Edit", editOptions, A_Clipboard)
    SetEditHorizontalPadding(edit.Hwnd, EditPaddingX)
    ApplyDarkControlTheme(edit.Hwnd)

    ; 所有关闭入口统一交给 CloseScratch，确保内容写回剪贴板。
    ScratchGui.OnEvent("Close", (*) => CloseScratch())
    ScratchGui.OnEvent("Escape", (*) => CloseScratch())
    ScratchGui.OnEvent("Size", ResizeScratchEditor)
    edit.OnEvent("Change", (*) => UpdateScratchScrollbar(edit))

    x := (A_ScreenWidth - WinW) // 2
    y := (A_ScreenHeight - WinH) // 2

    ; 使用 Windows 11 原生圆角和暗色窗口属性，并隐藏系统亮色细边框。
    ApplyWin11WindowStyle(ScratchGui.Hwnd)

    ShowScratchWithoutFlash(ScratchGui, edit, x, y, WinW, WinH)
    ScratchVisible := true

    edit.Focus()
}


ShowExistingScratch() {
    global ScratchGui, ScratchVisible

    edit := ScratchGui["ScratchText"]
    edit.Value := A_Clipboard

    ; 直接通过 Gui 对象读取隐藏窗口位置，不受 DetectHiddenWindows 设置影响。
    ScratchGui.GetPos(&x, &y)
    ShowScratchWithoutFlash(ScratchGui, edit, x, y)

    ScratchVisible := true
    edit.Focus()
}


ShowScratchWithoutFlash(guiObj, editCtrl, targetX, targetY, width := "", height := "") {
    ; 先在整个虚拟桌面之外真正显示，确保透明度 API 能作用于已显示窗口。
    stagingW := width != "" ? width : 1000
    stagingH := height != "" ? height : 800
    offscreenX := SysGet(76) - stagingW - 200 ; SM_XVIRTUALSCREEN
    offscreenY := SysGet(77) - stagingH - 200 ; SM_YVIRTUALSCREEN

    showOptions := "NA x" offscreenX " y" offscreenY
    if width != "" {
        showOptions .= " w" width
    }
    if height != "" {
        showOptions .= " h" height
    }

    guiObj.Show(showOptions)
    WinSetTransparent(0, "ahk_id " guiObj.Hwnd)

    ; 在完全透明时移到目标位置，处理目标显示器的 DPI/尺寸变化并重绘。
    WinMove(targetX, targetY,,, "ahk_id " guiObj.Hwnd)
    UpdateScratchScrollbar(editCtrl)
    RedrawScratchBeforeShow(guiObj.Hwnd)
    DllCall("dwmapi\DwmFlush")

    ; 只在完整暗色帧提交后显示，并移除分层样式以保持文字清晰度。
    WinSetTransparent(255, "ahk_id " guiObj.Hwnd)
    DllCall("dwmapi\DwmFlush")
    WinSetTransparent("Off", "ahk_id " guiObj.Hwnd)
    WinActivate("ahk_id " guiObj.Hwnd)
}


ResizeScratchEditor(guiObj, minMax, width, height) {
    global DragZoneH, Margin, HeaderHintW

    if (minMax = -1) {
        return
    }

    edit := guiObj["ScratchText"]
    edit.Move(
        ,,
        Max(1, width - Margin * 2),
        Max(1, height - DragZoneH - Margin)
    )
    guiObj["ScratchHint"].Move(width - Margin - HeaderHintW)
    UpdateScratchScrollbar(edit)
}


SetEditHorizontalPadding(hwnd, padding) {
    ; EM_SETMARGINS：为原生 Edit 控件增加左右留白。
    DllCall(
        "SendMessage",
        "Ptr", hwnd,
        "UInt", 0x00D3,
        "Ptr", 3,
        "Ptr", padding | (padding << 16)
    )
}


ApplyDarkControlTheme(hwnd) {
    ; 让原生 Edit 控件（包括其滚动条）使用 Windows 深色主题。
    DllCall(
        "uxtheme\SetWindowTheme",
        "Ptr", hwnd,
        "Str", "DarkMode_Explorer",
        "Ptr", 0
    )
}


UpdateScratchScrollbar(editCtrl) {
    ; EM_GETLINECOUNT 会把自动换行产生的可视行也计算在内。
    lineCount := DllCall(
        "SendMessage",
        "Ptr", editCtrl.Hwnd,
        "UInt", 0x00BA,
        "Ptr", 0,
        "Ptr", 0,
        "Ptr"
    )

    formatRect := Buffer(16, 0)
    DllCall(
        "SendMessage",
        "Ptr", editCtrl.Hwnd,
        "UInt", 0x00B2, ; EM_GETRECT
        "Ptr", 0,
        "Ptr", formatRect
    )

    viewportH := NumGet(formatRect, 12, "Int") - NumGet(formatRect, 4, "Int")
    contentH := lineCount * GetEditLineHeight(editCtrl.Hwnd)
    needsScrollbar := contentH > viewportH

    ; SB_VERT = 1。保留滚轮滚动能力，只按内容需要显示滚动条。
    DllCall(
        "ShowScrollBar",
        "Ptr", editCtrl.Hwnd,
        "Int", 1,
        "Int", needsScrollbar
    )
}


GetEditLineHeight(hwnd) {
    hdc := DllCall("GetDC", "Ptr", hwnd, "Ptr")
    if !hdc {
        return 1
    }

    font := DllCall(
        "SendMessage",
        "Ptr", hwnd,
        "UInt", 0x0031, ; WM_GETFONT
        "Ptr", 0,
        "Ptr", 0,
        "Ptr"
    )
    oldFont := font ? DllCall("SelectObject", "Ptr", hdc, "Ptr", font, "Ptr") : 0

    metrics := Buffer(60, 0)
    gotMetrics := DllCall("GetTextMetricsW", "Ptr", hdc, "Ptr", metrics)

    if oldFont {
        DllCall("SelectObject", "Ptr", hdc, "Ptr", oldFont, "Ptr")
    }
    DllCall("ReleaseDC", "Ptr", hwnd, "Ptr", hdc)

    if !gotMetrics {
        return 1
    }

    return NumGet(metrics, 0, "Int") + NumGet(metrics, 16, "Int")
}


RedrawScratchBeforeShow(hwnd) {
    ; 同步重绘客户区、子控件和边框；cloak 状态下不调用 DwmFlush，避免阻塞。
    DllCall(
        "RedrawWindow",
        "Ptr", hwnd,
        "Ptr", 0,
        "Ptr", 0,
        "UInt", 0x0585
    )
}


ScratchHitTest(wParam, lParam, msg, hwnd) {
    global ScratchGui, EdgeDragW

    if !IsObject(ScratchGui) || hwnd != ScratchGui.Hwnd {
        return
    }

    GetCursorScreenPos(&mx, &my)
    WinGetPos(&wx, &wy, &ww, &wh, "ahk_id " ScratchGui.Hwnd)

    relX := mx - wx
    relY := my - wy

    if (relX < 0 || relX >= ww || relY < 0 || relY >= wh) {
        return
    }

    onLeft := relX < EdgeDragW
    onRight := relX >= ww - EdgeDragW
    onTop := relY < EdgeDragW
    onBottom := relY >= wh - EdgeDragW

    if onTop {
        if onLeft {
            return 13 ; HTTOPLEFT
        }
        if onRight {
            return 14 ; HTTOPRIGHT
        }
        return 12 ; HTTOP
    }

    if onBottom {
        if onLeft {
            return 16 ; HTBOTTOMLEFT
        }
        if onRight {
            return 17 ; HTBOTTOMRIGHT
        }
        return 15 ; HTBOTTOM
    }

    if onLeft {
        return 10 ; HTLEFT
    }
    if onRight {
        return 11 ; HTRIGHT
    }
}


TryNativeDrag(wParam, lParam, msg, hwnd) {
    global ScratchGui
    global DragZoneH, EditTopDragExtra

    if !IsObject(ScratchGui) {
        return
    }

    GetCursorScreenPos(&mx, &my)

    WinGetPos(&wx, &wy, &ww, &wh, "ahk_id " ScratchGui.Hwnd)

    relX := mx - wx
    relY := my - wy

    if (relX < 0 || relX >= ww || relY < 0 || relY >= wh) {
        return
    }

    ; 可拖动区域包括：
    ; 1. 顶部拖动区
    ; 2. Edit 内部顶部额外区域
    isDragZone := relY < DragZoneH + EditTopDragExtra

    if !isDragZone {
        return
    }

    ; 伪装成标题栏拖动，交给 Windows 原生窗口移动逻辑。
    DllCall("ReleaseCapture")
    DllCall(
        "SendMessage",
        "Ptr", ScratchGui.Hwnd,
        "UInt", 0x00A1, ; WM_NCLBUTTONDOWN
        "Ptr", 2,      ; HTCAPTION
        "Ptr", 0
    )

    return 0
}


GetCursorScreenPos(&x, &y) {
    ; 直接调用 Win32 API，避免受 AHK 坐标模式影响。
    pt := Buffer(8, 0)
    DllCall("GetCursorPos", "Ptr", pt)

    x := NumGet(pt, 0, "Int")
    y := NumGet(pt, 4, "Int")
}


ApplyWin11WindowStyle(hwnd) {
    ; DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    darkMode := 1

    DllCall(
        "dwmapi\DwmSetWindowAttribute",
        "Ptr", hwnd,
        "UInt", 20,
        "Int*", darkMode,
        "UInt", 4
    )

    ; DWMWA_WINDOW_CORNER_PREFERENCE = 33
    ; DWMWCP_ROUND = 2
    cornerPreference := 2

    DllCall(
        "dwmapi\DwmSetWindowAttribute",
        "Ptr", hwnd,
        "UInt", 33,
        "Int*", cornerPreference,
        "UInt", 4
    )

    ; DWMWA_BORDER_COLOR = 34；DWMWA_COLOR_NONE 隐藏 DWM 绘制的亮色边框。
    borderColor := 0xFFFFFFFE

    DllCall(
        "dwmapi\DwmSetWindowAttribute",
        "Ptr", hwnd,
        "UInt", 34,
        "UInt*", borderColor,
        "UInt", 4
    )
}


CloseScratch() {
    global ScratchGui, ScratchVisible

    if IsObject(ScratchGui) && ScratchVisible {
        try SaveScratchToClipboard(ScratchGui["ScratchText"].Value)
        ScratchVisible := false
        try ScratchGui.Hide()
    }
}


SaveScratchToClipboard(text) {
    if (text = "") {
        return
    }

    A_Clipboard := text
}


CloseScratchOnExit(exitReason, exitCode) {
    global ScratchGui, ScratchVisible

    CloseScratch()

    if IsObject(ScratchGui) {
        try ScratchGui.Destroy()
        ScratchGui := ""
    }
    ScratchVisible := false
}
