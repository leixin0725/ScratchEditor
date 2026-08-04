[CmdletBinding()]
param(
    [string]$BuildSubdirectory = "build\release",
    [string]$ServerName = "ScratchEditor.Ahk.Validation",
    [string]$OriginalAhkPath = "",
    [string]$AutoHotkeyExecutable = "C:\Program Files\AutoHotkey\v2\AutoHotkey64.exe",
    [string]$ArtifactPrefix = "ahk-ipc"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$editorExe = Join-Path $projectRoot "$BuildSubdirectory\ScratchEditor.exe"
$ahkExe = $AutoHotkeyExecutable
$ahkBridge = Join-Path $projectRoot "tests\fixtures\KeysRedirect.IpcTest.ahk"
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = $env:SCRATCHEDITOR_ORIGINAL_AHK
}
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = Join-Path $projectRoot "dev-links\AutoHotkey\KeysRedirect.ahk"
}
$originalAhk = $OriginalAhkPath
$pipeName = $ServerName
$env:SCRATCHEDITOR_SERVER_NAME = $ServerName
$env:SCRATCHEDITOR_EXE = $editorExe
$env:SCRATCHEDITOR_SETTINGS_FILE = Join-Path ([System.IO.Path]::GetTempPath()) `
    "ScratchEditor\tests\ahk-validation.ini"

function Send-IpcCommand {
    param([Parameter(Mandatory)] [string]$Command, [int]$TimeoutMs = 2000)

    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", $pipeName, [System.IO.Pipes.PipeDirection]::InOut
    )
    try {
        $pipe.Connect($TimeoutMs)
        $encoding = [System.Text.UTF8Encoding]::new($false)
        $writer = [System.IO.StreamWriter]::new($pipe, $encoding, 4096, $true)
        $reader = [System.IO.StreamReader]::new($pipe, $encoding, $false, 4096, $true)
        try {
            $writer.WriteLine((@{ command = $Command } | ConvertTo-Json -Compress))
            $writer.Flush()
            return $reader.ReadLine() | ConvertFrom-Json
        }
        finally {
            $reader.Dispose()
            $writer.Dispose()
        }
    }
    finally {
        $pipe.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $ahkExe)) {
    throw "AutoHotkey v2 executable not found at $ahkExe"
}
if (-not (Test-Path -LiteralPath $editorExe)) {
    throw "ScratchEditor release build is missing."
}
if (-not (Test-Path -LiteralPath $ahkBridge)) {
    throw "AHK test fixture is missing at $ahkBridge"
}
if (-not (Test-Path -LiteralPath $originalAhk)) {
    throw "Original AHK source is missing. Pass -OriginalAhkPath or set SCRATCHEDITOR_ORIGINAL_AHK."
}

$hashBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $editorExe
$startInfo.Arguments = "--background --test-mode"
$startInfo.UseShellExecute = $false
$editorProcess = [System.Diagnostics.Process]::Start($startInfo)

try {
    $ready = $null
    for ($attempt = 0; $attempt -lt 200; $attempt++) {
        try {
            $candidate = Send-IpcCommand -Command "status" -TimeoutMs 20
            if ($candidate.ready -and [int]$candidate.pid -eq $editorProcess.Id) {
                $ready = $candidate
                break
            }
        }
        catch {
        }
        Start-Sleep -Milliseconds 5
    }
    if ($null -eq $ready) {
        throw "ScratchEditor did not become ready for the AHK IPC test."
    }

    $sequenceProcess = Start-Process -FilePath $ahkExe -ArgumentList @(
        $ahkBridge, "--sequence-test"
    ) -PassThru
    $shown = $null
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        $candidate = Send-IpcCommand -Command "status"
        if ($candidate.visible) {
            $shown = $candidate
            break
        }
        Start-Sleep -Milliseconds 5
    }
    if ($null -eq $shown) {
        throw "AHK persistent sequence did not show the editor."
    }
    $sequenceProcess.WaitForExit(3000) | Out-Null
    if ($sequenceProcess.ExitCode -ne 0) {
        throw "AHK persistent IPC sequence failed with exit code $($sequenceProcess.ExitCode)."
    }
    $hidden = Send-IpcCommand -Command "status"

    $hashAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
    $report = [ordered]@{
        timestamp = (Get-Date).ToString("o")
        ahkExecutable = $ahkExe
        bridge = $ahkBridge
        persistentConnectionSequence = $true
        showVisible = [bool]$shown.visible
        hideVisible = [bool]$hidden.visible
        originalHashBefore = $hashBefore
        originalHashAfter = $hashAfter
        originalUnchanged = ($hashBefore -eq $hashAfter)
        passed = ($shown.visible -and -not $hidden.visible -and $hashBefore -eq $hashAfter)
    }
    $reportJson = $report | ConvertTo-Json
    $artifactDir = Join-Path $projectRoot "artifacts"
    New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
    $artifactPath = Join-Path $artifactDir (
        "$ArtifactPrefix-{0}.json" -f (Get-Date -Format "yyyyMMdd-HHmmss")
    )
    [System.IO.File]::WriteAllText(
        $artifactPath, $reportJson, [System.Text.UTF8Encoding]::new($false)
    )
    $reportJson
    Write-Host "AHK IPC report: $artifactPath"
    if (-not $report.passed) {
        exit 1
    }
}
finally {
    try {
        $null = Send-IpcCommand -Command "quit" -TimeoutMs 500
    }
    catch {
        if (-not $editorProcess.HasExited) {
            Stop-Process -Id $editorProcess.Id
        }
    }
    $editorProcess.WaitForExit(2000) | Out-Null
}
