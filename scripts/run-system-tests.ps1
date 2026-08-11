[CmdletBinding()]
param(
    [string]$OriginalAhkPath = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\release"
$editorExe = Join-Path $buildDir "ScratchEditor.exe"
$systemExe = Join-Path $buildDir "ScratchEditorSystemTests.exe"
$ahkExe = "C:\Program Files\AutoHotkey\v2\AutoHotkey64.exe"
$ahkCopy = Join-Path $projectRoot "integration\KeysRedirect.QtMigration.ahk"
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = $env:SCRATCHEDITOR_ORIGINAL_AHK
}
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = Join-Path $projectRoot "integration\KeysRedirect.QtMigration.ahk"
}
$originalAhk = $OriginalAhkPath
$artifactDir = Join-Path $projectRoot "artifacts"
$pipeName = "ScratchEditor.Stage1.v1"

function Send-IpcRequest {
    param(
        [Parameter(Mandatory)] [hashtable]$Request,
        [int]$TimeoutMs = 3000
    )

    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", $pipeName, [System.IO.Pipes.PipeDirection]::InOut
    )
    try {
        $pipe.Connect($TimeoutMs)
        $encoding = [System.Text.UTF8Encoding]::new($false)
        $writer = [System.IO.StreamWriter]::new($pipe, $encoding, 4096, $true)
        $reader = [System.IO.StreamReader]::new($pipe, $encoding, $false, 4096, $true)
        try {
            $writer.WriteLine(($Request | ConvertTo-Json -Compress))
            $writer.Flush()
            $line = $reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($line)) {
                throw "IPC returned an empty response."
            }
            return $line | ConvertFrom-Json
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

function Send-IpcCommand {
    param([Parameter(Mandatory)] [string]$Command, [int]$TimeoutMs = 3000)
    Send-IpcRequest -Request @{ command = $Command } -TimeoutMs $TimeoutMs
}

function Stop-TestInstance {
    try {
        $status = Send-IpcCommand -Command "status" -TimeoutMs 100
        if (-not $status.testMode) {
            throw "A non-test ScratchEditor instance is running; refusing to stop it."
        }
        $null = Send-IpcCommand -Command "quit" -TimeoutMs 500
        Start-Sleep -Milliseconds 100
    }
    catch [System.TimeoutException] {
    }
    catch [System.IO.IOException] {
    }
}

function Start-TestInstance {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $editorExe
    $startInfo.Arguments = "--background --test-mode"
    $startInfo.UseShellExecute = $false
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_TEST_CLIPBOARD_BACKEND"] = "native"
    $process = [System.Diagnostics.Process]::Start($startInfo)

    $ready = $null
    for ($attempt = 0; $attempt -lt 300; $attempt++) {
        try {
            $candidate = Send-IpcCommand -Command "status" -TimeoutMs 20
            if ($candidate.ready -and [int]$candidate.pid -eq $process.Id) {
                $ready = $candidate
                break
            }
        }
        catch {
        }
        Start-Sleep -Milliseconds 5
    }
    if ($null -eq $ready) {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id
        }
        throw "ScratchEditor did not become ready for system tests."
    }
    [pscustomobject]@{ Process = $process; Status = $ready }
}

function Stop-StartedInstance {
    param([Parameter(Mandatory)] $Started)
    try {
        $null = Send-IpcCommand -Command "quit" -TimeoutMs 500
    }
    catch {
        if (-not $Started.Process.HasExited) {
            Stop-Process -Id $Started.Process.Id
        }
    }
    $Started.Process.WaitForExit(2000) | Out-Null
}

foreach ($required in @($editorExe, $systemExe, $ahkExe, $ahkCopy, $originalAhk)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required system test input is missing: $required"
    }
}

New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$hashBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
$repoStatusBefore = (& git -C (Split-Path -Parent $originalAhk) status --short --branch) -join "`n"
$started = $null
$secondStarted = $null

try {
    Stop-TestInstance
    $started = Start-TestInstance
    $null = Send-IpcCommand -Command "testResetSettings"
    $shown = Send-IpcCommand -Command "show"

    $expectedGeometry = [ordered]@{
        x = [int]$shown.x + 24
        y = [int]$shown.y + 18
        width = 760
        height = 520
    }
    $geometrySet = Send-IpcRequest -Request @{
        command = "testSetGeometry"
        x = $expectedGeometry.x
        y = $expectedGeometry.y
        width = $expectedGeometry.width
        height = $expectedGeometry.height
    }
    $null = Send-IpcCommand -Command "hide"
    Stop-StartedInstance -Started $started
    $started = $null

    $secondStarted = Start-TestInstance
    $restoredGeometry = Send-IpcCommand -Command "status"
    $geometryPassed = (
        [int]$restoredGeometry.x -eq $expectedGeometry.x -and
        [int]$restoredGeometry.y -eq $expectedGeometry.y -and
        [int]$restoredGeometry.width -eq $expectedGeometry.width -and
        [int]$restoredGeometry.height -eq $expectedGeometry.height -and
        [int]$restoredGeometry.settingsStatus -eq 0
    )

    $systemJson = (& $systemExe | Out-String).Trim()
    $systemExitCode = $LASTEXITCODE
    $systemBehavior = $systemJson | ConvertFrom-Json

    $ahkStart = [System.Diagnostics.ProcessStartInfo]::new()
    $ahkStart.FileName = $ahkExe
    $ahkStart.UseShellExecute = $false
    $ahkStart.RedirectStandardOutput = $true
    $ahkStart.RedirectStandardError = $true
    $ahkStart.Arguments = '"' + $ahkCopy + '" --migration-ipc-test'
    $ahkProcess = [System.Diagnostics.Process]::Start($ahkStart)
    if (-not $ahkProcess.WaitForExit(5000)) {
        Stop-Process -Id $ahkProcess.Id
        throw "The isolated AHK migration copy timed out."
    }
    $ahkStdout = $ahkProcess.StandardOutput.ReadToEnd().Trim()
    $ahkStderr = $ahkProcess.StandardError.ReadToEnd().Trim()
    $ahkStatus = Send-IpcCommand -Command "status"
    $ahkPassed = (
        $ahkProcess.ExitCode -eq 0 -and
        $ahkStdout -eq "qt-ipc-pass" -and
        -not $ahkStatus.visible
    )

    $copyText = [System.IO.File]::ReadAllText($ahkCopy, [System.Text.Encoding]::UTF8)
    $rollbackSwitchPresent = (
        $copyText.Contains("global UseQtScratchEditor := true") -and
        $copyText.Contains("if UseQtScratchEditor && EnsureQtScratchEditorAndSend(`"toggle`")") -and
        $copyText.Contains("    ToggleScratch()")
    )

    $diffText = (& git diff --no-index -- $originalAhk $ahkCopy 2>&1) -join "`n"
    $diffPath = Join-Path $artifactDir "system-ahk-copy-$timestamp.diff"
    [System.IO.File]::WriteAllText($diffPath, $diffText, [System.Text.UTF8Encoding]::new($false))

    $null = Send-IpcCommand -Command "testResetSettings"
    $hashAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
    $repoStatusAfter = (& git -C (Split-Path -Parent $originalAhk) status --short --branch) -join "`n"

    $checks = [ordered]@{
        geometryPersistence = $geometryPassed
        smartScrollbar = [bool]$systemBehavior.checks.smartScrollBar
        escapeClosesAndCopies = [bool]$systemBehavior.checks.escapeClosesAndCopies
        ctrlSClosesAndDelivers = [bool]$systemBehavior.checks.ctrlSClosesAndDelivers
        ctrlWClosesWithoutSaving = [bool]$systemBehavior.checks.ctrlWClosesWithoutSaving
        ctrlWIpcClosesWithoutSaving = [bool]$systemBehavior.checks.ctrlWIpcClosesWithoutSaving
        focusNotPulledBack = [bool]$systemBehavior.checks.focusNotPulledBack
        clipboardReadFailure = [bool]$systemBehavior.checks.clipboardReadFailureIsVisible
        clipboardWriteFailure = [bool]$systemBehavior.checks.clipboardWriteFailureKeepsEditor
        clipboardRecovery = [bool]$systemBehavior.checks.clipboardWriteRecovers
        isolatedAhkQtIpc = $ahkPassed
        rollbackSwitch = $rollbackSwitchPresent
        originalAhkUnchanged = ($hashBefore -eq $hashAfter)
        originalAhkRepoUnchanged = ($repoStatusBefore -eq $repoStatusAfter)
    }
    $allPassed = ($systemExitCode -eq 0 -and -not ($checks.Values -contains $false))
    $report = [ordered]@{
        timestamp = (Get-Date).ToString("o")
        geometry = [ordered]@{
            expected = $expectedGeometry
            setResponse = $geometrySet
            restored = $restoredGeometry
        }
        behavior = $systemBehavior
        ahk = [ordered]@{
            executable = $ahkExe
            isolatedCopy = $ahkCopy
            stdout = $ahkStdout
            stderr = $ahkStderr
            exitCode = $ahkProcess.ExitCode
            finalStatus = $ahkStatus
            diffArtifact = $diffPath
        }
        sourceProtection = [ordered]@{
            original = $originalAhk
            hashBefore = $hashBefore
            hashAfter = $hashAfter
            repoStatusBefore = $repoStatusBefore
            repoStatusAfter = $repoStatusAfter
        }
        checks = $checks
        allPassed = $allPassed
    }

    $artifactPath = Join-Path $artifactDir "system-results-$timestamp.json"
    [System.IO.File]::WriteAllText(
        $artifactPath,
        ($report | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false)
    )
    [pscustomobject]@{
        allPassed = $allPassed
        checks = $checks
        artifact = $artifactPath
        ahkDiffArtifact = $diffPath
    } | ConvertTo-Json -Depth 5

    if (-not $allPassed) {
        exit 1
    }
}
finally {
    if ($null -ne $secondStarted) {
        Stop-StartedInstance -Started $secondStarted
    }
    elseif ($null -ne $started) {
        Stop-StartedInstance -Started $started
    }
}
