[CmdletBinding()]
param(
    [string]$TestEditorPath,
    [switch]$StatusOnly,
    [switch]$ListCandidates
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
$messageFile = Join-Path $projectRoot "config\switch-ahk-editor.zh-CN.json"
if (-not (Test-Path -LiteralPath $messageFile -PathType Leaf)) {
    throw "Missing message resource: $messageFile"
}
$messageJson = [System.IO.File]::ReadAllText(
    $messageFile, [System.Text.UTF8Encoding]::new($false))
$script:messages = $messageJson | ConvertFrom-Json

function Get-Text {
    param(
        [Parameter(Mandatory, Position = 0)][string]$Key,
        [Parameter(Position = 1, ValueFromRemainingArguments = $true)][object[]]$Arguments
    )
    $property = $script:messages.PSObject.Properties[$Key]
    if ($null -eq $property) {
        throw "Missing message key: $Key"
    }
    $template = [string]$property.Value
    if ($null -eq $Arguments -or $Arguments.Count -eq 0) {
        return $template
    }
    return [string]::Format(
        [System.Globalization.CultureInfo]::CurrentCulture, $template, $Arguments)
}

$script:markerColors = @{
    Current = "Cyan"
    Target = "Magenta"
    Stopping = "Yellow"
    Stopped = "DarkYellow"
    Starting = "Blue"
    Active = "Green"
    Done = "Green"
    Warning = "Yellow"
    Error = "Red"
    Guide = "DarkGray"
    Select = "Cyan"
    Recovery = "Yellow"
    Recovered = "Green"
    RecoveryFailed = "Red"
    Candidates = "Cyan"
}
$script:markerKeys = @{
    Current = "markerCurrent"
    Target = "markerTarget"
    Stopping = "markerStopping"
    Stopped = "markerStopped"
    Starting = "markerStarting"
    Active = "markerActive"
    Done = "markerDone"
    Warning = "markerWarning"
    Error = "markerError"
    Guide = "markerGuide"
    Select = "markerSelect"
    Recovery = "markerRecovery"
    Recovered = "markerRecovered"
    RecoveryFailed = "markerRecoveryFailed"
    Candidates = "markerCandidates"
}

function Write-SwitchMessage {
    param(
        [Parameter(Mandatory)][ValidateSet(
            "Current", "Target", "Stopping", "Stopped", "Starting", "Active",
            "Done", "Warning", "Error", "Guide", "Select", "Recovery",
            "Recovered", "RecoveryFailed", "Candidates")][string]$Kind,
        [Parameter(Mandatory)][string]$Message
    )
    $marker = Get-Text $script:markerKeys[$Kind]
    Write-Host ("[{0}]" -f $marker) -ForegroundColor $script:markerColors[$Kind] -NoNewline
    Write-Host (" {0}" -f $Message)
}

function Write-SwitchDetail {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ("       {0}" -f $Message) -ForegroundColor Gray
}

$defaultServerName = "ScratchEditor.Stage1.v1"
$serverName = if ([string]::IsNullOrWhiteSpace($env:SCRATCHEDITOR_SWITCH_SERVER_NAME)) {
    $defaultServerName
}
else {
    $env:SCRATCHEDITOR_SWITCH_SERVER_NAME.Trim()
}
$stableEditor = if ([string]::IsNullOrWhiteSpace($env:SCRATCHEDITOR_SWITCH_STABLE_EDITOR)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw (Get-Text "localAppDataMissing")
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
$exitDelayMilliseconds = 2000
if ($isolatedMode -and
        -not [string]::IsNullOrWhiteSpace($env:SCRATCHEDITOR_SWITCH_EXIT_DELAY_MS)) {
    $parsedExitDelay = 0
    if (-not [int]::TryParse(
            $env:SCRATCHEDITOR_SWITCH_EXIT_DELAY_MS, [ref]$parsedExitDelay) -or
            $parsedExitDelay -lt 0 -or $parsedExitDelay -gt 10000) {
        throw (Get-Text "invalidExitDelay")
    }
    $exitDelayMilliseconds = $parsedExitDelay
}
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

function Get-ModeDisplayName {
    param([Parameter(Mandatory)][ValidateSet("NONE", "STABLE", "TEST", "UNKNOWN")][string]$Mode)
    switch ($Mode) {
        "NONE" { return (Get-Text "modeNone") }
        "STABLE" { return (Get-Text "modeStable") }
        "TEST" { return (Get-Text "modeTest") }
        default { return (Get-Text "modeUnknown") }
    }
}

function Get-BooleanDisplayName {
    param([bool]$Value)
    return Get-Text $(if ($Value) { "yes" } else { "no" })
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
        Write-SwitchMessage -Kind $Label -Message (Get-Text "state" `
            (Get-ModeDisplayName $mode) $Status.pid `
            (Get-BooleanDisplayName ([bool]$Status.visible)) `
            (Get-BooleanDisplayName ([bool]$Status.ready)))
        Write-SwitchDetail (Get-Text "path" $Status.executableFile)
        return
    }
    if (-not [string]::IsNullOrWhiteSpace($TargetPath)) {
        Write-SwitchMessage -Kind $Label -Message `
            (Get-ModeDisplayName (Get-EditorMode $TargetPath))
        Write-SwitchDetail (Get-Text "path" $TargetPath)
        return
    }
    Write-SwitchMessage -Kind $Label -Message (Get-Text "noResident" $serverName)
}

function Assert-EditorTarget {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Role
    )

    $fullPath = Get-NormalizedPath $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw (Get-Text "targetMissing" (Get-ModeDisplayName $Role) $fullPath)
    }
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals(
            [System.IO.Path]::GetFileName($fullPath), "ScratchEditor.exe")) {
        throw (Get-Text "targetInvalidName" (Get-ModeDisplayName $Role) $fullPath)
    }
    return (Resolve-Path -LiteralPath $fullPath).Path
}

function Get-RegisteredWorktreePaths {
    $paths = [System.Collections.Generic.List[string]]::new()
    $paths.Add($projectRoot)
    $testPaths = if ($isolatedMode) {
        $env:SCRATCHEDITOR_SWITCH_TEST_WORKTREES
    }
    else {
        $null
    }
    if (-not [string]::IsNullOrWhiteSpace($testPaths)) {
        foreach ($path in $testPaths.Split([System.IO.Path]::PathSeparator)) {
            if (-not [string]::IsNullOrWhiteSpace($path)) {
                $paths.Add([System.IO.Path]::GetFullPath($path))
            }
        }
    }
    else {
        $output = @(& git -C $projectRoot worktree list --porcelain 2>$null)
        if ($LASTEXITCODE -eq 0) {
            foreach ($line in $output) {
                if ($line.StartsWith("worktree ", [StringComparison]::Ordinal)) {
                    $paths.Add([System.IO.Path]::GetFullPath($line.Substring(9)))
                }
            }
        }
        else {
            Write-SwitchMessage -Kind Warning -Message (Get-Text "worktreeScanFailed")
        }
    }
    return @($paths | Select-Object -Unique)
}

function Get-TestEditorCandidates {
    $candidates = [System.Collections.Generic.List[object]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($worktreePath in Get-RegisteredWorktreePaths) {
        if (-not (Test-Path -LiteralPath $worktreePath -PathType Container)) {
            continue
        }
        $worktreeItem = Get-Item -LiteralPath $worktreePath
        if (($worktreeItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            Write-SwitchMessage -Kind Warning -Message (Get-Text "reparseSkipped" $worktreePath)
            continue
        }
        $buildRoot = Join-Path $worktreePath "build"
        if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
            continue
        }
        $buildRootItem = Get-Item -LiteralPath $buildRoot
        if (($buildRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            Write-SwitchMessage -Kind Warning -Message (Get-Text "reparseSkipped" $buildRoot)
            continue
        }
        foreach ($buildDirectory in Get-ChildItem -LiteralPath $buildRoot -Directory `
                -ErrorAction SilentlyContinue) {
            if (($buildDirectory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                Write-SwitchMessage -Kind Warning -Message `
                    (Get-Text "reparseSkipped" $buildDirectory.FullName)
                continue
            }
            $candidatePath = Join-Path $buildDirectory.FullName "ScratchEditor.exe"
            if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
                continue
            }
            $candidate = Get-Item -LiteralPath $candidatePath
            if (-not $seen.Add($candidate.FullName)) {
                continue
            }
            $worktreeName = if (Test-SamePath $worktreePath $projectRoot) {
                [System.IO.Path]::GetFileName($projectRoot)
            }
            else {
                [System.IO.Path]::GetFileName($worktreePath)
            }
            $candidates.Add([pscustomobject]@{
                FullName = $candidate.FullName
                LastWriteTime = $candidate.LastWriteTime
                BuildName = $buildDirectory.Name
                WorktreeName = $worktreeName
                WorktreePath = $worktreePath
            })
        }
    }
    return @($candidates | Sort-Object LastWriteTime -Descending)
}

function Write-TestEditorCandidates {
    param([Parameter(Mandatory)][object[]]$Candidates)
    Write-SwitchMessage -Kind Candidates -Message (Get-Text "candidateTitle")
    for ($index = 0; $index -lt $Candidates.Count; $index++) {
        $item = $Candidates[$index]
        Write-Host (Get-Text "candidateSummary" ($index + 1) $item.WorktreeName `
            $item.BuildName $item.LastWriteTime) -ForegroundColor White
        Write-SwitchDetail (Get-Text "worktree" $item.WorktreePath)
        Write-SwitchDetail (Get-Text "candidatePath" $item.FullName)
    }
}

function Select-TestEditor {
    $candidates = @(Get-TestEditorCandidates)
    if ($candidates.Count -eq 0) {
        Write-SwitchMessage -Kind Guide -Message (Get-Text "noCandidates")
        Write-SwitchMessage -Kind Guide -Message (Get-Text "buildFirst")
        Write-SwitchDetail (Get-Text "buildCommand")
        throw (Get-Text "noCandidateAvailable")
    }

    Write-TestEditorCandidates -Candidates $candidates
    if ($nonInteractive) {
        throw (Get-Text "nonInteractivePathRequired")
    }
    while ($true) {
        $answer = Read-Host (Get-Text "chooseCandidate" $candidates.Count)
        if ($answer -match '^[Qq]$') {
            throw (Get-Text "cancelled")
        }
        $selection = 0
        if ([int]::TryParse($answer, [ref]$selection) -and
                $selection -ge 1 -and $selection -le $candidates.Count) {
            return $candidates[$selection - 1].FullName
        }
        Write-SwitchMessage -Kind Guide -Message (Get-Text "chooseCandidateAgain")
    }
}

function Select-ModeWithoutResident {
    if ($nonInteractive) {
        throw (Get-Text "noResidentNonInteractive")
    }
    Write-SwitchMessage -Kind Select -Message (Get-Text "noResidentChoose")
    Write-SwitchDetail (Get-Text "startStable")
    Write-SwitchDetail (Get-Text "startTest")
    while ($true) {
        $answer = Read-Host (Get-Text "chooseMode")
        switch -Regex ($answer) {
            '^1$' { return "STABLE" }
            '^2$' { return "TEST" }
            '^[Qq]$' { throw (Get-Text "cancelled") }
            default {
                Write-SwitchMessage -Kind Guide -Message (Get-Text "chooseModeAgain")
            }
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
        Write-SwitchMessage -Kind Stopping -Message `
            (Get-Text "waitingIsolated" $Identity.Pid)
        $idle = Invoke-EditorCommand -Command "testWaitForClipboardHistoryIdle" -TimeoutMs 6000
        if ($null -eq $idle -or -not [bool]$idle.ok) {
            throw (Get-Text "isolatedNotIdle")
        }
        $freshStatus = Invoke-EditorCommand -Command "status" -TimeoutMs 500
        if (-not (Test-ProcessIdentity -Expected $Identity -Status $freshStatus)) {
            throw (Get-Text "isolatedIdentityChanged")
        }
        Stop-Process -Id $Identity.Pid -Force -ErrorAction Stop
        if (-not (Wait-ForProcessExit -ProcessId $Identity.Pid -TimeoutMs 2000)) {
            throw (Get-Text "isolatedStopFailed")
        }
        Write-SwitchMessage -Kind Stopped -Message (Get-Text "isolatedStopped")
        return
    }

    Write-SwitchMessage -Kind Stopping -Message (Get-Text "normalShutdown" $Identity.Pid)
    $null = Invoke-EditorCommand -Command "shutdownForUpdate" -TimeoutMs 1000
    if (Wait-ForProcessExit -ProcessId $Identity.Pid) {
        Write-SwitchMessage -Kind Stopped -Message (Get-Text "normalStopped")
        return
    }

    $freshStatus = Invoke-EditorCommand -Command "status" -TimeoutMs 500
    if (-not (Test-ProcessIdentity -Expected $Identity -Status $freshStatus)) {
        throw (Get-Text "oldIdentityLost")
    }
    Write-SwitchMessage -Kind Warning -Message (Get-Text "normalTimeout")
    Write-SwitchDetail (Get-Text "verifiedPid" $Identity.Pid)
    Write-SwitchDetail (Get-Text "verifiedPath" $Identity.Path)
    if ($nonInteractive) {
        throw (Get-Text "forceNeedsInteractive")
    }
    $answer = Read-Host (Get-Text "forcePrompt")
    if ($answer -notmatch '^[Yy]$') {
        throw (Get-Text "cancelStillRunning")
    }
    Stop-Process -Id $Identity.Pid -Force -ErrorAction Stop
    if (-not (Wait-ForProcessExit -ProcessId $Identity.Pid -TimeoutMs 2000)) {
        throw (Get-Text "forceStopFailed")
    }
    Write-SwitchMessage -Kind Stopped -Message (Get-Text "forceStopped")
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
                    throw (Get-Text "pipePathMismatch" $Path $status.executableFile)
                }
                if ([int]$status.pid -ne $process.Id) {
                    throw (Get-Text "pipePidMismatch" $process.Id $status.pid)
                }
                if (-not [bool]$status.ready) {
                    continue
                }
                return $status
            }
            if ($process.HasExited) {
                throw (Get-Text "targetExited" $process.ExitCode $Path)
            }
        }
        throw (Get-Text "targetNotReady" $Path)
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
    if ($ListCandidates) {
        $candidates = @(Get-TestEditorCandidates)
        if ($candidates.Count -eq 0) {
            Write-SwitchMessage -Kind Guide -Message (Get-Text "noCandidates")
            Write-SwitchMessage -Kind Guide -Message (Get-Text "buildFirst")
            Write-SwitchDetail (Get-Text "buildCommand")
            return
        }
        Write-TestEditorCandidates -Candidates $candidates
        Write-SwitchMessage -Kind Done -Message (Get-Text "listOnlyDone" $candidates.Count)
        return
    }

    $currentStatus = Invoke-EditorCommand -Command "status"
    if ($null -eq $currentStatus -and
            ($script:lastPipeConnectionState -in @("ConnectedNoResponse", "AccessDenied") -or
                (Test-ServerPipeExists))) {
        $reason = if ($script:lastPipeConnectionState -eq "AccessDenied") {
            Get-Text "reasonAccessDenied"
        }
        elseif ($script:lastPipeConnectionState -eq "ConnectedNoResponse") {
            Get-Text "reasonNoResponse"
        }
        else {
            Get-Text "reasonUnavailable"
        }
        Write-SwitchMessage -Kind Current -Message `
            (Get-Text "unknownCurrent" $serverName $reason)
        if ($script:lastPipeConnectionState -eq "AccessDenied") {
            Write-SwitchMessage -Kind Guide -Message (Get-Text "runElevated")
        }
        throw (Get-Text "unsafeCurrent")
    }
    Write-EditorState -Label "CURRENT" -Status $currentStatus

    if ($StatusOnly) {
        if ($null -eq $currentStatus) {
            Write-SwitchMessage -Kind Guide -Message (Get-Text "statusGuide")
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
        throw (Get-Text "sameAsStable")
    }
    Write-EditorState -Label "TARGET" -Status $null -TargetPath $targetPath

    $previousIdentity = $null
    $previousPath = $null
    if ($null -ne $currentStatus) {
        $previousIdentity = Get-ProcessIdentity $currentStatus
        if ($null -eq $previousIdentity) {
            throw (Get-Text "currentIdentityFailed")
        }
        $previousPath = Assert-EditorTarget -Path ([string]$currentStatus.executableFile) `
            -Role $currentMode
        Stop-CurrentEditor -Identity $previousIdentity `
            -UseIsolatedTermination:($isolatedMode -and [bool]$currentStatus.testMode)
    }

    try {
        Write-SwitchMessage -Kind Starting -Message (Get-Text "starting" $targetPath)
        $activeStatus = Start-EditorTarget -Path $targetPath
        Write-EditorState -Label "ACTIVE" -Status $activeStatus
        Write-SwitchMessage -Kind Done -Message (Get-Text "routingDone")
    }
    catch {
        $targetError = $_.Exception.Message
        Write-SwitchMessage -Kind Error -Message (Get-Text "targetStartupFailed" $targetError)
        if (-not [string]::IsNullOrWhiteSpace($previousPath) -and
                (Test-Path -LiteralPath $previousPath -PathType Leaf)) {
            Write-SwitchMessage -Kind Recovery -Message (Get-Text "restoring" $previousPath)
            try {
                $restoredStatus = Start-EditorTarget -Path $previousPath
                Write-EditorState -Label "ACTIVE" -Status $restoredStatus
                Write-SwitchMessage -Kind Recovered -Message (Get-Text "restored")
            }
            catch {
                Write-SwitchMessage -Kind RecoveryFailed -Message `
                    (Get-Text "recoveryFailed" $_.Exception.Message)
                Write-SwitchMessage -Kind Guide -Message (Get-Text "startStableManually")
                Write-SwitchDetail ("& '{0}' --background" -f $stableEditor.Replace("'", "''"))
            }
        }
        throw $targetError
    }
}

$exitCode = 0
try {
    Invoke-Switch
}
catch {
    Write-SwitchMessage -Kind Error -Message $_.Exception.Message
    Write-SwitchMessage -Kind Guide -Message (Get-Text "inspectStatus")
    Write-SwitchDetail "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\switch-ahk-editor.ps1 -StatusOnly"
    $exitCode = 1
}
finally {
    if ($exitDelayMilliseconds -gt 0) {
        Write-SwitchMessage -Kind Guide -Message `
            (Get-Text "exitDelay" ($exitDelayMilliseconds / 1000))
        Start-Sleep -Milliseconds $exitDelayMilliseconds
    }
}
exit $exitCode
