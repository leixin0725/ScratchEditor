[CmdletBinding()]
param(
    [string]$TestEditorPath,
    [switch]$StatusOnly
)

$ErrorActionPreference = "Stop"
# Windows PowerShell 5.1 PipeStream has no reliable read timeout. Peek before
# synchronous reads so an unhealthy or older resident cannot block forever.
if (-not ("ScratchEditorPipeNative" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class ScratchEditorPipeNative
{
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PeekNamedPipe(
        IntPtr handle,
        IntPtr buffer,
        uint bufferSize,
        IntPtr bytesRead,
        out uint totalBytesAvailable,
        IntPtr bytesLeftThisMessage);
}
"@
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$defaultServerName = "ScratchEditor.Stage1.v1"
$serverName = if ([string]::IsNullOrWhiteSpace($env:SCRATCHEDITOR_SWITCH_SERVER_NAME)) {
    $defaultServerName
}
else {
    $env:SCRATCHEDITOR_SWITCH_SERVER_NAME.Trim()
}
$stableEditor = if ([string]::IsNullOrWhiteSpace($env:SCRATCHEDITOR_SWITCH_STABLE_EDITOR)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw "LOCALAPPDATA is not available."
    }
    Join-Path $env:LOCALAPPDATA "ScratchEditor\AhkEditor\ScratchEditor.exe"
}
else {
    $env:SCRATCHEDITOR_SWITCH_STABLE_EDITOR
}
$isolatedMode = -not [StringComparer]::Ordinal.Equals($serverName, $defaultServerName)
$nonInteractive = $env:SCRATCHEDITOR_SWITCH_NONINTERACTIVE -eq "1" -or
    -not [Environment]::UserInteractive -or [Console]::IsInputRedirected
$isolatedSettingsFile = $env:SCRATCHEDITOR_SWITCH_SETTINGS_FILE
$script:lastPipeConnectionState = "Unavailable"

function Get-NormalizedPath {
    param([Parameter(Mandatory)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}

function Test-SamePath {
    param([string]$Left, [string]$Right)
    if ([string]::IsNullOrWhiteSpace($Left) -or [string]::IsNullOrWhiteSpace($Right)) {
        return $false
    }
    return [StringComparer]::OrdinalIgnoreCase.Equals(
        (Get-NormalizedPath $Left), (Get-NormalizedPath $Right))
}

function Get-EditorMode {
    param([string]$ExecutablePath)
    if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
        return "NONE"
    }
    if (Test-SamePath $ExecutablePath $stableEditor) {
        return "STABLE"
    }
    return "TEST"
}

function Invoke-EditorCommand {
    param(
        [Parameter(Mandatory)][string]$Command,
        [int]$TimeoutMs = 750
    )

    $script:lastPipeConnectionState = "Unavailable"
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", $serverName, [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $pipe.Connect($TimeoutMs)
        $script:lastPipeConnectionState = "ConnectedNoResponse"
        $encoding = [System.Text.UTF8Encoding]::new($false)
        $payload = @{ command = $Command; requestId = "editor-switch" } |
            ConvertTo-Json -Compress
        $payloadBytes = $encoding.GetBytes($payload + "`n")
        $pipe.Write($payloadBytes, 0, $payloadBytes.Length)
        $pipe.Flush()

        $response = [System.IO.MemoryStream]::new()
        $timer = [System.Diagnostics.Stopwatch]::StartNew()
        while ($timer.ElapsedMilliseconds -lt $TimeoutMs) {
            [uint32]$available = 0
            $peeked = [ScratchEditorPipeNative]::PeekNamedPipe(
                $pipe.SafePipeHandle.DangerousGetHandle(),
                [IntPtr]::Zero, 0, [IntPtr]::Zero, [ref]$available, [IntPtr]::Zero)
            if (-not $peeked) {
                return $null
            }
            if ($available -gt 0) {
                $buffer = New-Object byte[] ([Math]::Min([int]$available, 65536))
                $count = $pipe.Read($buffer, 0, $buffer.Length)
                if ($count -le 0) {
                    return $null
                }
                $response.Write($buffer, 0, $count)
                $text = $encoding.GetString($response.ToArray())
                $newline = $text.IndexOf("`n")
                if ($newline -ge 0) {
                    $line = $text.Substring(0, $newline).Trim()
                    if ([string]::IsNullOrWhiteSpace($line)) {
                        return $null
                    }
                    $result = $line | ConvertFrom-Json
                    $script:lastPipeConnectionState = "Responded"
                    return $result
                }
            }
            Start-Sleep -Milliseconds 10
        }
        return $null
    }
    catch [System.TimeoutException] {
        return $null
    }
    catch [System.IO.IOException] {
        return $null
    }
    catch [System.UnauthorizedAccessException] {
        $script:lastPipeConnectionState = "AccessDenied"
        return $null
    }
    finally {
        $pipe.Dispose()
    }
}

function Test-ServerPipeExists {
    try {
        $match = [System.IO.Directory]::GetFiles("\\.\pipe\") |
            Where-Object {
                [StringComparer]::OrdinalIgnoreCase.Equals(
                    [System.IO.Path]::GetFileName($_), $serverName)
            } |
            Select-Object -First 1
        return $null -ne $match
    }
    catch {
        return $false
    }
}

function Write-EditorState {
    param(
        [Parameter(Mandatory)][string]$Label,
        $Status,
        [string]$TargetPath
    )

    if ($null -ne $Status) {
        $mode = Get-EditorMode ([string]$Status.executableFile)
        Write-Host ("[{0}] {1} | PID {2} | visible={3} | ready={4}" -f
            $Label, $mode, $Status.pid, ([bool]$Status.visible), ([bool]$Status.ready))
        Write-Host ("          Path: {0}" -f $Status.executableFile)
        return
    }
    if (-not [string]::IsNullOrWhiteSpace($TargetPath)) {
        Write-Host ("[{0}] {1}" -f $Label, (Get-EditorMode $TargetPath))
        Write-Host ("          Path: {0}" -f $TargetPath)
        return
    }
    Write-Host ("[{0}] NONE | no resident owns pipe {1}" -f $Label, $serverName)
}

function Assert-EditorTarget {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Role
    )

    $fullPath = Get-NormalizedPath $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "$Role target does not exist: $fullPath"
    }
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals(
            [System.IO.Path]::GetFileName($fullPath), "ScratchEditor.exe")) {
        throw "$Role target must be a ScratchEditor.exe file: $fullPath"
    }
    return (Resolve-Path -LiteralPath $fullPath).Path
}

function Select-TestEditor {
    $candidates = @(
        Get-ChildItem -LiteralPath (Join-Path $projectRoot "build") -Directory `
            -ErrorAction SilentlyContinue |
            ForEach-Object {
                $candidate = Join-Path $_.FullName "ScratchEditor.exe"
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    Get-Item -LiteralPath $candidate
                }
            } |
            Sort-Object LastWriteTime -Descending
    )
    if ($candidates.Count -eq 0) {
        Write-Host "[GUIDE] No test builds were found under build/*/ScratchEditor.exe."
        Write-Host "        Build one without installing it:"
        Write-Host "        powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset editing -SkipLocalInstall"
        throw "No test editor candidate is available."
    }

    Write-Host "[SELECT] Available test builds:"
    for ($index = 0; $index -lt $candidates.Count; $index++) {
        $item = $candidates[$index]
        Write-Host ("  {0}. {1} | {2:yyyy-MM-dd HH:mm:ss}" -f
            ($index + 1), $item.Directory.Name, $item.LastWriteTime)
        Write-Host ("     {0}" -f $item.FullName)
    }
    if ($nonInteractive) {
        throw "A test target must be supplied with -TestEditorPath in non-interactive mode."
    }
    while ($true) {
        $answer = Read-Host ("Choose a test build [1-{0}], or Q to cancel" -f $candidates.Count)
        if ($answer -match '^[Qq]$') {
            throw "Switch cancelled."
        }
        $selection = 0
        if ([int]::TryParse($answer, [ref]$selection) -and
                $selection -ge 1 -and $selection -le $candidates.Count) {
            return $candidates[$selection - 1].FullName
        }
        Write-Host "[GUIDE] Enter one of the listed numbers, or Q to cancel."
    }
}

function Select-ModeWithoutResident {
    if ($nonInteractive) {
        throw "No resident is running. Supply -TestEditorPath or run interactively to choose a mode."
    }
    Write-Host "[SELECT] No resident is running."
    Write-Host "  1. Start STABLE"
    Write-Host "  2. Start TEST"
    while ($true) {
        $answer = Read-Host "Choose 1 or 2, or Q to cancel"
        switch -Regex ($answer) {
            '^1$' { return "STABLE" }
            '^2$' { return "TEST" }
            '^[Qq]$' { throw "Switch cancelled." }
            default { Write-Host "[GUIDE] Enter 1, 2, or Q." }
        }
    }
}

function Get-ProcessIdentity {
    param($Status)
    if ($null -eq $Status -or [int]$Status.pid -le 0 -or
            [string]::IsNullOrWhiteSpace([string]$Status.executableFile)) {
        return $null
    }
    $process = Get-Process -Id ([int]$Status.pid) -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        return $null
    }
    return [pscustomobject]@{
        Pid = [int]$Status.pid
        Path = Get-NormalizedPath ([string]$Status.executableFile)
        StartTimeUtcTicks = $process.StartTime.ToUniversalTime().Ticks
    }
}

function Test-ProcessIdentity {
    param(
        [Parameter(Mandatory)]$Expected,
        $Status
    )
    if ($null -eq $Status -or [int]$Status.pid -ne $Expected.Pid -or
            -not (Test-SamePath ([string]$Status.executableFile) $Expected.Path)) {
        return $false
    }
    $process = Get-Process -Id $Expected.Pid -ErrorAction SilentlyContinue
    return $null -ne $process -and
        $process.StartTime.ToUniversalTime().Ticks -eq $Expected.StartTimeUtcTicks
}

function Wait-ForProcessExit {
    param([Parameter(Mandatory)][int]$ProcessId, [int]$TimeoutMs = 3000)
    $attempts = [Math]::Max(1, [int][Math]::Ceiling($TimeoutMs / 50.0))
    for ($attempt = 0; $attempt -lt $attempts; $attempt++) {
        if ($null -eq (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) {
            return $true
        }
        Start-Sleep -Milliseconds 50
    }
    return $null -eq (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)
}

function Stop-CurrentEditor {
    param(
        [Parameter(Mandatory)]$Identity,
        [switch]$UseIsolatedTermination
    )

    if ($UseIsolatedTermination) {
        Write-Host ("[STOPPING] Waiting for isolated test work before stopping PID {0}." -f
            $Identity.Pid)
        $idle = Invoke-EditorCommand -Command "testWaitForClipboardHistoryIdle" -TimeoutMs 6000
        if ($null -eq $idle -or -not [bool]$idle.ok) {
            throw "The isolated clipboard-history coordinator did not become idle; nothing was stopped."
        }
        $freshStatus = Invoke-EditorCommand -Command "status" -TimeoutMs 500
        if (-not (Test-ProcessIdentity -Expected $Identity -Status $freshStatus)) {
            throw "The isolated process identity changed; nothing was stopped."
        }
        Stop-Process -Id $Identity.Pid -Force -ErrorAction Stop
        if (-not (Wait-ForProcessExit -ProcessId $Identity.Pid -TimeoutMs 2000)) {
            throw "The verified isolated process did not exit after Stop-Process."
        }
        Write-Host "[STOPPED] Verified isolated test process was stopped."
        return
    }

    Write-Host ("[STOPPING] Requesting a normal save and shutdown for PID {0}." -f
        $Identity.Pid)
    $null = Invoke-EditorCommand -Command "shutdownForUpdate" -TimeoutMs 1000
    if (Wait-ForProcessExit -ProcessId $Identity.Pid) {
        Write-Host "[STOPPED] Previous resident exited normally."
        return
    }

    $freshStatus = Invoke-EditorCommand -Command "status" -TimeoutMs 500
    if (-not (Test-ProcessIdentity -Expected $Identity -Status $freshStatus)) {
        throw "The old process did not exit and its identity can no longer be verified. No process was forced to stop."
    }
    Write-Host "[WARNING] Normal shutdown timed out."
    Write-Host ("          Verified PID: {0}" -f $Identity.Pid)
    Write-Host ("          Verified path: {0}" -f $Identity.Path)
    if ($nonInteractive) {
        throw "Forced termination requires an interactive confirmation."
    }
    $answer = Read-Host "Force-stop only this verified PID? [y/N]"
    if ($answer -notmatch '^[Yy]$') {
        throw "Switch cancelled; the previous resident is still running."
    }
    Stop-Process -Id $Identity.Pid -Force -ErrorAction Stop
    if (-not (Wait-ForProcessExit -ProcessId $Identity.Pid -TimeoutMs 2000)) {
        throw "The verified process did not exit after Stop-Process."
    }
    Write-Host "[STOPPED] Verified previous resident was force-stopped."
}

function Start-EditorTarget {
    param([Parameter(Mandatory)][string]$Path)

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Path
    $startInfo.Arguments = if ($isolatedMode) { "--background --test-mode" } else { "--background" }
    $startInfo.WorkingDirectory = Split-Path -Parent $Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    if ($isolatedMode) {
        $startInfo.EnvironmentVariables["SCRATCHEDITOR_SERVER_NAME"] = $serverName
        if (-not [string]::IsNullOrWhiteSpace($isolatedSettingsFile)) {
            $startInfo.EnvironmentVariables["SCRATCHEDITOR_SETTINGS_FILE"] = $isolatedSettingsFile
        }
    }
    else {
        foreach ($name in @(
                "SCRATCHEDITOR_SERVER_NAME", "SCRATCHEDITOR_SETTINGS_FILE",
                "SCRATCHEDITOR_UI_CONFIG", "SCRATCHEDITOR_MARKDOWN_STYLE",
                "SCRATCHEDITOR_TEST_CLIPBOARD_BACKEND",
                "SCRATCHEDITOR_EXTERNAL_TEST_STATUS_FILE",
                "SCRATCHEDITOR_EXTERNAL_TEST_TEXT",
                "SCRATCHEDITOR_EXTERNAL_TEST_DISCARD"
            )) {
            $startInfo.EnvironmentVariables.Remove($name)
        }
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        $startupTimer = [System.Diagnostics.Stopwatch]::StartNew()
        while ($startupTimer.ElapsedMilliseconds -lt 5000) {
            Start-Sleep -Milliseconds 50
            $status = Invoke-EditorCommand -Command "status" -TimeoutMs 100
            if ($null -ne $status) {
                if (-not (Test-SamePath ([string]$status.executableFile) $Path)) {
                    throw "Pipe ownership mismatch: expected $Path, actual $($status.executableFile)"
                }
                if ([int]$status.pid -ne $process.Id) {
                    throw "Pipe PID mismatch: expected $($process.Id), actual $($status.pid)"
                }
                if (-not [bool]$status.ready) {
                    continue
                }
                return $status
            }
            if ($process.HasExited) {
                throw "Target exited during startup with code $($process.ExitCode): $Path"
            }
        }
        throw "Target did not become ready within 5 seconds: $Path"
    }
    catch {
        if (-not $process.HasExited) {
            $process.Kill()
            $null = $process.WaitForExit(2000)
        }
        throw
    }
}

function Invoke-Switch {
    $currentStatus = Invoke-EditorCommand -Command "status"
    if ($null -eq $currentStatus -and
            ($script:lastPipeConnectionState -in @("ConnectedNoResponse", "AccessDenied") -or
                (Test-ServerPipeExists))) {
        $reason = if ($script:lastPipeConnectionState -eq "AccessDenied") {
            "access denied"
        }
        elseif ($script:lastPipeConnectionState -eq "ConnectedNoResponse") {
            "no status response"
        }
        else {
            "pipe is present but unavailable"
        }
        Write-Host ("[CURRENT] UNKNOWN | {0} ({1})" -f $serverName, $reason)
        if ($script:lastPipeConnectionState -eq "AccessDenied") {
            Write-Host "[GUIDE] Re-run the command from an elevated PowerShell window."
        }
        throw "The current resident cannot be identified safely; nothing was changed."
    }
    Write-EditorState -Label "CURRENT" -Status $currentStatus

    if ($StatusOnly) {
        if ($null -eq $currentStatus) {
            Write-Host "[GUIDE] Run without -StatusOnly to choose and start a resident."
        }
        return
    }

    $currentMode = if ($null -eq $currentStatus) {
        "NONE"
    }
    else {
        Get-EditorMode ([string]$currentStatus.executableFile)
    }

    $targetMode = $null
    $targetPath = $null
    if ($currentMode -eq "TEST") {
        $targetMode = "STABLE"
        $targetPath = $stableEditor
    }
    elseif ($currentMode -eq "STABLE") {
        $targetMode = "TEST"
        $targetPath = if ([string]::IsNullOrWhiteSpace($TestEditorPath)) {
            Select-TestEditor
        }
        else {
            $TestEditorPath
        }
    }
    elseif (-not [string]::IsNullOrWhiteSpace($TestEditorPath)) {
        $targetMode = "TEST"
        $targetPath = $TestEditorPath
    }
    else {
        $targetMode = Select-ModeWithoutResident
        $targetPath = if ($targetMode -eq "STABLE") {
            $stableEditor
        }
        else {
            Select-TestEditor
        }
    }

    $targetPath = Assert-EditorTarget -Path $targetPath -Role $targetMode
    if ($targetMode -eq "TEST" -and (Test-SamePath $targetPath $stableEditor)) {
        throw "The test target resolves to the stable installation. Choose a build output instead."
    }
    Write-EditorState -Label "TARGET" -Status $null -TargetPath $targetPath

    $previousIdentity = $null
    $previousPath = $null
    if ($null -ne $currentStatus) {
        $previousIdentity = Get-ProcessIdentity $currentStatus
        if ($null -eq $previousIdentity) {
            throw "The current resident identity could not be verified; nothing was stopped."
        }
        $previousPath = Assert-EditorTarget -Path ([string]$currentStatus.executableFile) -Role "Current"
        Stop-CurrentEditor -Identity $previousIdentity `
            -UseIsolatedTermination:($isolatedMode -and [bool]$currentStatus.testMode)
    }

    try {
        Write-Host ("[STARTING] {0}" -f $targetPath)
        $activeStatus = Start-EditorTarget -Path $targetPath
        Write-EditorState -Label "ACTIVE" -Status $activeStatus
        Write-Host "[DONE] Hotkeys now route to the active resident shown above."
    }
    catch {
        $targetError = $_.Exception.Message
        Write-Host ("[ERROR] Target startup failed: {0}" -f $targetError)
        if (-not [string]::IsNullOrWhiteSpace($previousPath) -and
                (Test-Path -LiteralPath $previousPath -PathType Leaf)) {
            Write-Host ("[RECOVERY] Restoring previous resident: {0}" -f $previousPath)
            try {
                $restoredStatus = Start-EditorTarget -Path $previousPath
                Write-EditorState -Label "ACTIVE" -Status $restoredStatus
                Write-Host "[RECOVERED] The previous resident is active again."
            }
            catch {
                Write-Host ("[RECOVERY-FAILED] {0}" -f $_.Exception.Message)
                Write-Host "[GUIDE] Start the stable resident manually:"
                Write-Host ("        & '{0}' --background" -f $stableEditor.Replace("'", "''"))
            }
        }
        throw $targetError
    }
}

try {
    Invoke-Switch
    exit 0
}
catch {
    Write-Host ("[ERROR] {0}" -f $_.Exception.Message)
    Write-Host "[GUIDE] Inspect the current state with:"
    Write-Host "        powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 -StatusOnly"
    exit 1
}
