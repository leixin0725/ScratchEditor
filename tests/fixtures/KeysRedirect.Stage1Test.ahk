#Requires AutoHotkey v2.0
#SingleInstance Force

; 阶段 1 独立测试桥。不要把本文件覆盖到正在使用的 KeysRedirect.ahk。

serverName := EnvGet("SCRATCHEDITOR_SERVER_NAME")
if !serverName
    serverName := "ScratchEditor.Stage1.v1"
editorExeFromEnvironment := EnvGet("SCRATCHEDITOR_EXE")
global ScratchEditorPipe := "\\.\pipe\" serverName
global ScratchEditorExe := editorExeFromEnvironment
    ? editorExeFromEnvironment
    : A_ScriptDir "\..\..\build\release\ScratchEditor.exe"
global ScratchEditorPipeHandle := -1
OnExit((*) => CloseScratchEditorPipe())

if A_Args.Length >= 2 && A_Args[1] = "--send-once" {
    ExitApp(EnsureScratchEditorAndSend(A_Args[2]) ? 0 : 1)
}
if A_Args.Length >= 1 && A_Args[1] = "--sequence-test" {
    shown := EnsureScratchEditorAndSend("show")
    Sleep(700)
    hidden := EnsureScratchEditorAndSend("hide")
    ExitApp(shown && hidden ? 0 : 1)
}

EnsureScratchEditorResident()
Hotkey("ScrollLock", (*) => EnsureScratchEditorAndSend("toggle"))

EnsureScratchEditorResident() {
    global ScratchEditorExe

    if SendScratchEditorCommand("ping") {
        return true
    }
    if !FileExist(ScratchEditorExe) {
        return false
    }

    Run('"' ScratchEditorExe '" --background')
    global ScratchEditorPipe
    return DllCall("WaitNamedPipeW", "Str", ScratchEditorPipe, "UInt", 1500)
}

EnsureScratchEditorAndSend(command) {
    if SendScratchEditorCommand(command) {
        return true
    }
    if !EnsureScratchEditorResident() {
        return false
    }
    return SendScratchEditorCommand(command)
}

SendScratchEditorCommand(command) {
    global ScratchEditorPipe, ScratchEditorPipeHandle

    if ScratchEditorPipeHandle = -1 {
        ScratchEditorPipeHandle := DllCall(
            "CreateFileW",
            "Str", ScratchEditorPipe,
            "UInt", 0x40000000, ; GENERIC_WRITE
            "UInt", 0,
            "Ptr", 0,
            "UInt", 3,          ; OPEN_EXISTING
            "UInt", 0,
            "Ptr", 0,
            "Ptr"
        )
    }
    if ScratchEditorPipeHandle = -1 {
        return false
    }

    payloadChars := StrPut(command "`n", "UTF-8")
    payload := Buffer(payloadChars)
    payloadBytes := StrPut(command "`n", payload, "UTF-8") - 1
    bytesWritten := 0
    ok := DllCall(
        "WriteFile",
        "Ptr", ScratchEditorPipeHandle,
        "Ptr", payload,
        "UInt", payloadBytes,
        "UInt*", &bytesWritten,
        "Ptr", 0
    )
    if !ok || bytesWritten != payloadBytes {
        CloseScratchEditorPipe()
        return false
    }
    return true
}

CloseScratchEditorPipe() {
    global ScratchEditorPipeHandle
    if ScratchEditorPipeHandle != -1 {
        DllCall("CloseHandle", "Ptr", ScratchEditorPipeHandle)
        ScratchEditorPipeHandle := -1
    }
}
