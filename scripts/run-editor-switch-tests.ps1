[CmdletBinding()]
param(
    [string]$StableBuildSubdirectory = "build\editing",
    [string]$TestBuildSubdirectory = "build\window-ui"
)

$ErrorActionPreference = "Stop"
if (-not ("ScratchEditorSwitchTestPipeNative" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class ScratchEditorSwitchTestPipeNative
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
$switchScript = Join-Path $PSScriptRoot "switch-ahk-editor.ps1"
$stableEditor = Join-Path $projectRoot "$StableBuildSubdirectory\ScratchEditor.exe"
$testEditor = Join-Path $projectRoot "$TestBuildSubdirectory\ScratchEditor.exe"
$serverName = "ScratchEditor.Switch.Validation." + [guid]::NewGuid().ToString("N")
$tempDirectory = Join-Path $env:TEMP (
    "ScratchEditor-Switch\validation-" + [guid]::NewGuid().ToString("N")
)
$settingsFile = Join-Path $tempDirectory "settings.ini"
$testWorktreeDirectory = Join-Path $tempDirectory "registered-worktree"
$testWorktreeEditor = Join-Path $testWorktreeDirectory "build\worktree-test\ScratchEditor.exe"

function Invoke-EditorCommand {
    param(
        [Parameter(Mandatory)][string]$Command,
        [int]$TimeoutMs = 500
    )

    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", $serverName, [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $pipe.Connect($TimeoutMs)
        $encoding = [System.Text.UTF8Encoding]::new($false)
        $payload = @{ command = $Command; requestId = "switch-test" } |
            ConvertTo-Json -Compress
        $payloadBytes = $encoding.GetBytes($payload + "`n")
        $pipe.Write($payloadBytes, 0, $payloadBytes.Length)
        $pipe.Flush()

        $response = [System.IO.MemoryStream]::new()
        $timer = [System.Diagnostics.Stopwatch]::StartNew()
        while ($timer.ElapsedMilliseconds -lt $TimeoutMs) {
            [uint32]$available = 0
            $peeked = [ScratchEditorSwitchTestPipeNative]::PeekNamedPipe(
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
                    return $line | ConvertFrom-Json
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
    finally {
        $pipe.Dispose()
    }
}

function Invoke-SwitchScript {
    param(
        [string[]]$Arguments,
        [switch]$CaptureOutput,
        [switch]$UseRealExitDelay
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = "powershell.exe"
    $quotedArguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        ('"' + $switchScript + '"')
    ) + @($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + $_.Replace('"', '\"') + '"'
        }
        else {
            $_
        }
    })
    $startInfo.Arguments = $quotedArguments -join " "
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $CaptureOutput
    $startInfo.RedirectStandardError = $CaptureOutput
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_SWITCH_SERVER_NAME"] = $serverName
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_SWITCH_STABLE_EDITOR"] = $stableEditor
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_SWITCH_SETTINGS_FILE"] = $settingsFile
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_SWITCH_NONINTERACTIVE"] = "1"
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_SWITCH_TEST_WORKTREES"] =
        $testWorktreeDirectory
    if (-not $UseRealExitDelay) {
        $startInfo.EnvironmentVariables["SCRATCHEDITOR_SWITCH_EXIT_DELAY_MS"] = "0"
    }
    else {
        $startInfo.EnvironmentVariables.Remove("SCRATCHEDITOR_SWITCH_EXIT_DELAY_MS")
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = if ($CaptureOutput) { $process.StandardOutput.ReadToEndAsync() } else { $null }
    $stderrTask = if ($CaptureOutput) { $process.StandardError.ReadToEndAsync() } else { $null }
    if (-not $process.WaitForExit(15000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Switch script timed out; PID $($process.Id)."
    }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = if ($CaptureOutput) { $stdoutTask.Result.Trim() } else { "" }
        Stderr = if ($CaptureOutput) { $stderrTask.Result.Trim() } else { "" }
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)]$Actual,
        [Parameter(Mandatory)]$Expected
    )
    if (-not $Condition) {
        throw "FAIL [$Name] actual=[$Actual] expected=[$Expected]"
    }
    Write-Host "PASS [$Name]"
}

function Test-SamePath {
    param([string]$Left, [string]$Right)
    if ([string]::IsNullOrWhiteSpace($Left) -or [string]::IsNullOrWhiteSpace($Right)) {
        return $false
    }
    return [StringComparer]::OrdinalIgnoreCase.Equals(
        [System.IO.Path]::GetFullPath($Left),
        [System.IO.Path]::GetFullPath($Right))
}

foreach ($path in @($stableEditor, $testEditor)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required isolated editor build is missing: $path"
    }
}

New-Item -ItemType Directory -Path $tempDirectory -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $testWorktreeEditor) -Force |
    Out-Null
Copy-Item -LiteralPath $testEditor -Destination $testWorktreeEditor
$startedPids = [System.Collections.Generic.HashSet[int]]::new()

try {
    $delayTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $delayedList = Invoke-SwitchScript -Arguments @("-ListCandidates") `
        -CaptureOutput -UseRealExitDelay
    $delayTimer.Stop()
    Assert-True "script keeps exit window open for two seconds" `
        ($delayedList.ExitCode -eq 0 -and $delayTimer.ElapsedMilliseconds -ge 1900) `
        ("exit={0}, elapsedMs={1}" -f $delayedList.ExitCode, $delayTimer.ElapsedMilliseconds) `
        "exit 0, elapsedMs >= 1900"

    $listed = Invoke-SwitchScript -Arguments @("-ListCandidates") -CaptureOutput
    Assert-True "registered worktree candidate is listed" `
        ($listed.ExitCode -eq 0 -and $listed.Stdout.Contains("registered-worktree") -and
            $listed.Stdout.Contains("worktree-test\ScratchEditor.exe")) `
        ($listed.Stdout + $listed.Stderr) "registered-worktree/build/worktree-test"

    $startTest = Invoke-SwitchScript -Arguments @("-TestEditorPath", $testEditor)
    Assert-True "no instance starts explicit test target" ($startTest.ExitCode -eq 0) `
        $startTest.ExitCode "exit 0"
    $status = Invoke-EditorCommand -Command "status" -TimeoutMs 1000
    Assert-True "explicit test target owns isolated pipe" `
        ($null -ne $status -and (Test-SamePath $status.executableFile $testEditor)) `
        $status.executableFile $testEditor
    $null = $startedPids.Add([int]$status.pid)

    $statusOnly = Invoke-SwitchScript -Arguments @("-StatusOnly")
    Assert-True "status-only succeeds" ($statusOnly.ExitCode -eq 0) `
        $statusOnly.ExitCode "exit 0"
    $statusAfterQuery = Invoke-EditorCommand -Command "status"
    Assert-True "status-only does not replace process" `
        ([int]$statusAfterQuery.pid -eq [int]$status.pid) `
        $statusAfterQuery.pid $status.pid

    $toStable = Invoke-SwitchScript -Arguments @()
    Assert-True "test switches to stable" ($toStable.ExitCode -eq 0) `
        $toStable.ExitCode "exit 0"
    $stableStatus = Invoke-EditorCommand -Command "status" -TimeoutMs 1000
    Assert-True "stable target owns isolated pipe" `
        ($null -ne $stableStatus -and (Test-SamePath $stableStatus.executableFile $stableEditor)) `
        $stableStatus.executableFile $stableEditor
    $null = $startedPids.Add([int]$stableStatus.pid)

    $scanOnly = Invoke-SwitchScript -Arguments @()
    Assert-True "non-interactive candidate scan fails safely" `
        ($scanOnly.ExitCode -ne 0) $scanOnly.ExitCode "nonzero"
    $stableAfterScan = Invoke-EditorCommand -Command "status"
    Assert-True "candidate scan does not stop stable pid" `
        ([int]$stableAfterScan.pid -eq [int]$stableStatus.pid) `
        $stableAfterScan.pid $stableStatus.pid

    $missingEditor = Join-Path $tempDirectory "missing\ScratchEditor.exe"
    $invalid = Invoke-SwitchScript -Arguments @("-TestEditorPath", $missingEditor)
    Assert-True "invalid target fails before stopping current instance" `
        ($invalid.ExitCode -ne 0) $invalid.ExitCode "nonzero"
    $stableAfterInvalid = Invoke-EditorCommand -Command "status"
    Assert-True "invalid target preserves stable pid" `
        ([int]$stableAfterInvalid.pid -eq [int]$stableStatus.pid) `
        $stableAfterInvalid.pid $stableStatus.pid

    $invalidDirectory = Join-Path $tempDirectory "invalid-target"
    New-Item -ItemType Directory -Path $invalidDirectory -Force | Out-Null
    $invalidExecutable = Join-Path $invalidDirectory "ScratchEditor.exe"
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot "System32\where.exe") `
        -Destination $invalidExecutable
    $failedStartup = Invoke-SwitchScript -Arguments @("-TestEditorPath", $invalidExecutable)
    Assert-True "failed target reports failure after recovery" `
        ($failedStartup.ExitCode -ne 0) $failedStartup.ExitCode "nonzero"
    $restoredStable = Invoke-EditorCommand -Command "status" -TimeoutMs 1000
    Assert-True "failed target restores stable path" `
        ($null -ne $restoredStable -and
            (Test-SamePath $restoredStable.executableFile $stableEditor)) `
        $restoredStable.executableFile $stableEditor
    $null = $startedPids.Add([int]$restoredStable.pid)

    $toTest = Invoke-SwitchScript -Arguments @("-TestEditorPath", $testEditor)
    Assert-True "stable switches to explicit test" ($toTest.ExitCode -eq 0) `
        $toTest.ExitCode "exit 0"
    $finalStatus = Invoke-EditorCommand -Command "status" -TimeoutMs 1000
    Assert-True "final test target verified by path" `
        ($null -ne $finalStatus -and (Test-SamePath $finalStatus.executableFile $testEditor)) `
        $finalStatus.executableFile $testEditor
    $null = $startedPids.Add([int]$finalStatus.pid)

    Write-Host "Editor switch validation passed."
}
finally {
    $current = Invoke-EditorCommand -Command "status" -TimeoutMs 100
    if ($null -ne $current -and $startedPids.Contains([int]$current.pid)) {
        $null = Invoke-EditorCommand -Command "testWaitForClipboardHistoryIdle" -TimeoutMs 6000
        Stop-Process -Id ([int]$current.pid) -Force -ErrorAction SilentlyContinue
        for ($attempt = 0; $attempt -lt 60; $attempt++) {
            if ($null -eq (Get-Process -Id ([int]$current.pid) -ErrorAction SilentlyContinue)) {
                break
            }
            Start-Sleep -Milliseconds 50
        }
    }
    $resolvedTemp = [System.IO.Path]::GetFullPath($tempDirectory)
    $resolvedTempRoot = [System.IO.Path]::GetFullPath((Join-Path $env:TEMP "ScratchEditor-Switch"))
    if ($resolvedTemp.StartsWith($resolvedTempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force -ErrorAction SilentlyContinue
    }
}
