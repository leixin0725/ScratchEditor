[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$SourcePath,

    [Parameter(Mandatory)]
    [string]$OutputPath,

    [string]$ScratchEditorExecutable =
        (Join-Path $env:LOCALAPPDATA "ScratchEditor\AhkEditor\ScratchEditor.exe"),

    [string]$ExpectedSourceSha256 =
        "8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9"
)

$ErrorActionPreference = "Stop"
$source = (Resolve-Path -LiteralPath $SourcePath).Path
$output = [System.IO.Path]::GetFullPath($OutputPath)
if ($source -eq $output) {
    throw "The stage 6 candidate must be generated at an isolated path."
}
if (-not (Test-Path -LiteralPath $ScratchEditorExecutable -PathType Leaf)) {
    throw "ScratchEditor executable not found: $ScratchEditorExecutable"
}

$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ($sourceHash -ne $ExpectedSourceSha256) {
    throw "Source hash mismatch. Expected $ExpectedSourceSha256, got $sourceHash."
}

$text = [System.IO.File]::ReadAllText($source, [System.Text.Encoding]::UTF8)
$newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

$oldIndexLine = "; 6. 临时编辑器：Ctrl+Alt+V 用剪贴板内容打开置顶草稿窗口"
$newIndexLine = "; 6. 临时编辑器：ScrollLock 通过本地 IPC 切换 Qt 编辑器"
if (($text.Split(@($oldIndexLine), [System.StringSplitOptions]::None).Count - 1) -ne 1) {
    throw "Expected exactly one legacy module-index line."
}
$text = $text.Replace($oldIndexLine, $newIndexLine)

$globalAnchor = @(
    "global LastActiveWindow := 0",
    "global IsTogglingObsidian := false"
) -join $newline
$escapedExecutable = $ScratchEditorExecutable.Replace('"', '""')
$globalReplacement = @(
    "global LastActiveWindow := 0",
    "global IsTogglingObsidian := false",
    "global QtScratchEditorPipeName := EnvGet(`"SCRATCHEDITOR_SERVER_NAME`")",
    "if QtScratchEditorPipeName = `"`"",
    "    QtScratchEditorPipeName := `"ScratchEditor.Stage1.v1`"",
    "global QtScratchEditorPipe := `"\\.\pipe\`" QtScratchEditorPipeName",
    "global QtScratchEditorExe := EnvGet(`"SCRATCHEDITOR_EXE`")",
    "if QtScratchEditorExe = `"`"",
    "    QtScratchEditorExe := `"$escapedExecutable`"",
    "global QtScratchEditorPipeHandle := -1",
    "global QtScratchEditorTestMode := EnvGet(`"SCRATCHEDITOR_STAGE6_TEST`") = `"1`"",
    "OnExit((*) => CloseQtScratchEditorPipe())",
    "",
    "; 隔离验证入口；正常启动时不会执行，也不会替换当前运行的主脚本。",
    "if EnvGet(`"SCRATCHEDITOR_STAGE6_TEST`") = `"1`"",
    "        && A_Args.Length >= 2 && A_Args[1] = `"--scratch-editor-stage6-test`" {",
    "    mode := A_Args[2]",
    "    if mode = `"ipc`" {",
    "        shown := EnsureQtScratchEditorAndSend(`"show`")",
    "        Sleep 300",
    "        hidden := EnsureQtScratchEditorAndSend(`"hide`")",
    "        stopped := EnsureQtScratchEditorAndSend(`"quit`")",
    "        passed := shown && hidden && stopped",
    "        result := passed ? `"stage6-ipc-pass``n`" : Format(",
    "            `"stage6-ipc-fail show={} hide={} quit={}``n`", shown, hidden, stopped)",
    "        FileAppend(result, `"*`")",
    "        CloseQtScratchEditorPipe()",
    "        ExitApp(passed ? 0 : 1)",
    "    }",
    "    if mode = `"fallback`" {",
    "        savedClipboard := ClipboardAll()",
    "        token := `"ScratchEditor stage 6 clipboard fallback`"",
    "        A_Clipboard := token",
    "        handled := ToggleScratchEditor()",
    "        preserved := A_Clipboard = token",
    "        A_Clipboard := savedClipboard",
    "        HideScratchEditorFallbackNotice()",
    "        passed := !handled && preserved",
    "        FileAppend(passed ? `"stage6-fallback-pass``n`" : `"stage6-fallback-fail``n`", `"*`")",
    "        ExitApp(passed ? 0 : 1)",
    "    }",
    "    FileAppend(`"stage6-test-mode-invalid``n`", `"*`")",
    "    ExitApp(2)",
    "}"
) -join $newline
if (($text.Split(@($globalAnchor), [System.StringSplitOptions]::None).Count - 1) -ne 1) {
    throw "Expected exactly one globals anchor."
}
$text = $text.Replace($globalAnchor, $globalReplacement)

$timerAnchor = "SetTimer EnforceCapsLockOff, 2000"
$timerReplacement = @(
    $timerAnchor,
    "SetTimer StartQtScratchEditorResident, -1"
) -join $newline
if (($text.Split(@($timerAnchor), [System.StringSplitOptions]::None).Count - 1) -ne 1) {
    throw "Expected exactly one startup-timer anchor."
}
$text = $text.Replace($timerAnchor, $timerReplacement)

$sectionMarker = @(
    "; ==============================================================================",
    "; 6. 临时编辑器：F14",
    "; =============================================================================="
) -join $newline
$sectionIndex = $text.IndexOf($sectionMarker, [System.StringComparison]::Ordinal)
if ($sectionIndex -lt 0 -or $text.IndexOf(
        $sectionMarker, $sectionIndex + $sectionMarker.Length,
        [System.StringComparison]::Ordinal) -ge 0) {
    throw "Expected exactly one legacy scratch-editor section."
}

$stage6Section = @(
    "; ==============================================================================" ,
    "; 6. 临时编辑器：ScrollLock -> Qt IPC",
    "; ==============================================================================" ,
    "",
    "; Qt 启动或 IPC 失败时不创建 AHK GUI、不改写剪贴板，只提示可直接粘贴原内容。",
    "ScrollLock::ToggleScratchEditor()",
    "",
    "",
    "ToggleScratchEditor() {",
    "    if EnsureQtScratchEditorAndSend(`"toggle`") {",
    "        return true",
    "    }",
    "",
    "    ScratchEditorClipboardFallback()",
    "    return false",
    "}",
    "",
    "",
    "StartQtScratchEditorResident() {",
    "    EnsureQtScratchEditorResident()",
    "}",
    "",
    "",
    "EnsureQtScratchEditorResident() {",
    "    global QtScratchEditorExe, QtScratchEditorPipe, QtScratchEditorTestMode",
    "",
    "    if IsQtScratchEditorReady() {",
    "        return true",
    "    }",
    "    if !FileExist(QtScratchEditorExe) {",
    "        return false",
    "    }",
    "",
    "    try {",
    "        arguments := QtScratchEditorTestMode ? `" --background --test-mode`" : `" --background`"",
    "        Run(Chr(34) QtScratchEditorExe Chr(34) arguments)",
    "    } catch {",
    "        return false",
    "    }",
    "",
    "    deadline := A_TickCount + 1500",
    "    while A_TickCount < deadline {",
    "        if IsQtScratchEditorReady() {",
    "            return true",
    "        }",
    "        Sleep 10",
    "    }",
    "    return false",
    "}",
    "",
    "",
    "IsQtScratchEditorReady() {",
    "    global QtScratchEditorPipe",
    "",
    "    handle := DllCall(",
    "        `"CreateFileW`",",
    "        `"Str`", QtScratchEditorPipe,",
    "        `"UInt`", 0xC0000000, ; GENERIC_READ | GENERIC_WRITE",
    "        `"UInt`", 0,",
    "        `"Ptr`", 0,",
    "        `"UInt`", 3,",
    "        `"UInt`", 0,",
    "        `"Ptr`", 0,",
    "        `"Ptr`"",
    "    )",
    "    if handle = -1 {",
    "        return false",
    "    }",
    "",
    '    request := "{''command'':''status'',''requestId'':''ahk-ready''}"',
    '    request := StrReplace(request, "''", Chr(34))',
    '    payloadChars := StrPut(request "`n", "UTF-8")',
    "    payload := Buffer(payloadChars)",
    '    payloadBytes := StrPut(request "`n", payload, "UTF-8") - 1',
    "    bytesWritten := 0",
    "    wrote := DllCall(",
    "        `"WriteFile`", `"Ptr`", handle, `"Ptr`", payload, `"UInt`", payloadBytes,",
    "        `"UInt*`", &bytesWritten, `"Ptr`", 0",
    "    )",
    "    if !wrote || bytesWritten != payloadBytes {",
    "        DllCall(`"CloseHandle`", `"Ptr`", handle)",
    "        return false",
    "    }",
    "",
    "    response := Buffer(65536, 0)",
    "    bytesRead := 0",
    "    read := DllCall(",
    "        `"ReadFile`", `"Ptr`", handle, `"Ptr`", response, `"UInt`", response.Size,",
    "        `"UInt*`", &bytesRead, `"Ptr`", 0",
    "    )",
    "    DllCall(`"CloseHandle`", `"Ptr`", handle)",
    "    if !read || bytesRead = 0 {",
    "        return false",
    "    }",
    "",
    "    status := StrGet(response, bytesRead, `"UTF-8`")",
    "    return InStr(status, '`"ready`":true') > 0",
    "}",
    "",
    "",
    "EnsureQtScratchEditorAndSend(command) {",
    "    if SendQtScratchEditorCommand(command) {",
    "        return true",
    "    }",
    "    if !EnsureQtScratchEditorResident() {",
    "        return false",
    "    }",
    "    return SendQtScratchEditorCommand(command)",
    "}",
    "",
    "",
    "SendQtScratchEditorCommand(command) {",
    "    global QtScratchEditorPipe, QtScratchEditorPipeHandle",
    "",
    "    if QtScratchEditorPipeHandle = -1 {",
    "        QtScratchEditorPipeHandle := DllCall(",
    "            `"CreateFileW`",",
    "            `"Str`", QtScratchEditorPipe,",
    "            `"UInt`", 0x40000000, ; GENERIC_WRITE",
    "            `"UInt`", 0,",
    "            `"Ptr`", 0,",
    "            `"UInt`", 3,          ; OPEN_EXISTING",
    "            `"UInt`", 0,",
    "            `"Ptr`", 0,",
    "            `"Ptr`"",
    "        )",
    "    }",
    "    if QtScratchEditorPipeHandle = -1 {",
    "        return false",
    "    }",
    "",
    '    payloadChars := StrPut(command "`n", "UTF-8")',
    "    payload := Buffer(payloadChars)",
    '    payloadBytes := StrPut(command "`n", payload, "UTF-8") - 1',
    "    bytesWritten := 0",
    "    ok := DllCall(",
    "        `"WriteFile`",",
    "        `"Ptr`", QtScratchEditorPipeHandle,",
    "        `"Ptr`", payload,",
    "        `"UInt`", payloadBytes,",
    "        `"UInt*`", &bytesWritten,",
    "        `"Ptr`", 0",
    "    )",
    "    if !ok || bytesWritten != payloadBytes {",
    "        CloseQtScratchEditorPipe()",
    "        return false",
    "    }",
    "    return true",
    "}",
    "",
    "",
    "CloseQtScratchEditorPipe() {",
    "    global QtScratchEditorPipeHandle",
    "",
    "    if QtScratchEditorPipeHandle != -1 {",
    "        DllCall(`"CloseHandle`", `"Ptr`", QtScratchEditorPipeHandle)",
    "        QtScratchEditorPipeHandle := -1",
    "    }",
    "}",
    "",
    "",
    "ScratchEditorClipboardFallback() {",
    "    ToolTip(`"ScratchEditor 启动失败；剪贴板内容未更改，可直接粘贴使用。`")",
    "    SetTimer(HideScratchEditorFallbackNotice, -2500)",
    "}",
    "",
    "",
    "HideScratchEditorFallbackNotice() {",
    "    ToolTip()",
    "}"
) -join $newline

$candidate = $text.Substring(0, $sectionIndex) + $stage6Section + $newline
if ($candidate.Contains("ScratchGui") -or $candidate.Contains("Gui(")) {
    throw "Legacy AHK GUI references remain in the candidate."
}
if (-not $candidate.Contains("ScrollLock::ToggleScratchEditor()") -or
        -not $candidate.Contains("EnsureQtScratchEditorAndSend") -or
        -not $candidate.Contains("ScratchEditorClipboardFallback")) {
    throw "The stage 6 IPC or clipboard fallback contract is incomplete."
}

$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
[System.IO.File]::WriteAllText($output, $candidate, [System.Text.UTF8Encoding]::new($false))

$outputHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
[pscustomobject]@{
    source = $source
    output = $output
    sourceSha256 = $sourceHash
    outputSha256 = $outputHash
    sourceLength = (Get-Item -LiteralPath $source).Length
    outputLength = (Get-Item -LiteralPath $output).Length
    executable = (Resolve-Path -LiteralPath $ScratchEditorExecutable).Path
    legacyGuiRemoved = $true
    ipcPreserved = $true
    clipboardFallbackPreserved = $true
}
