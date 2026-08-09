param(
    [string]$BuildSubdirectory = "build\editing",
    [string]$ServerName = "ScratchEditor.ClipboardHistory.Validation"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildSubdirectory))
$executable = Join-Path $buildPath "ScratchEditor.exe"
$testExecutable = Join-Path $buildPath "ScratchEditorClipboardHistoryTests.exe"
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("ScratchEditor-ClipboardHistory-" + [guid]::NewGuid().ToString("N"))
$settingsFile = Join-Path $temporaryRoot "settings.ini"
$historyFile = Join-Path $temporaryRoot "clipboard-history.dat"
$startedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$previousServer = $env:SCRATCHEDITOR_SERVER_NAME
$previousSettings = $env:SCRATCHEDITOR_SETTINGS_FILE

function Invoke-HistoryRequest {
    param([string]$Command, [hashtable]$Arguments = @{}, [int]$TimeoutMs = 5000)
    $request = @{} + $Arguments
    $request.command = $Command
    $pipe = [IO.Pipes.NamedPipeClientStream]::new(
        ".", $ServerName, [IO.Pipes.PipeDirection]::InOut,
        [IO.Pipes.PipeOptions]::None)
    try {
        $pipe.Connect($TimeoutMs)
        $encoding = [Text.UTF8Encoding]::new($false)
        $writer = [IO.StreamWriter]::new($pipe, $encoding)
        $writer.AutoFlush = $true
        $reader = [IO.StreamReader]::new($pipe, $encoding)
        $writer.WriteLine(($request | ConvertTo-Json -Compress -Depth 8))
        $read = $reader.ReadLineAsync()
        if (!$read.Wait($TimeoutMs)) {
            throw "$Command response timed out"
        }
        return $read.Result | ConvertFrom-Json
    } finally {
        $pipe.Dispose()
    }
}

function Start-IsolatedEditor {
    $process = Start-Process -FilePath $executable `
        -ArgumentList @("--test-mode", "--background") `
        -PassThru -WindowStyle Hidden
    $startedProcesses.Add($process)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($process.HasExited) {
            throw "隔离实例在 ready 前退出，退出码 $($process.ExitCode)"
        }
        try {
            $status = Invoke-HistoryRequest -Command "status" -TimeoutMs 250
            if ($status.ready -and [int]$status.pid -eq $process.Id) {
                if ($status.clipboardBackend -ne "memory" -or
                    [int64]$status.nativeClipboardAccessAttempts -ne 0) {
                    throw "隔离实例违反 clipboard backend 约束"
                }
                $idle = Invoke-HistoryRequest -Command "testWaitForClipboardHistoryIdle" `
                    -Arguments @{ timeoutMs = 10000 }
                if (!$idle.ok) {
                    throw "隔离 history store 未在 10 秒内完成加载"
                }
                return $process
            }
        } catch {
            Start-Sleep -Milliseconds 50
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "隔离实例未在 15 秒内 ready"
}

function Stop-IsolatedEditor {
    param([Diagnostics.Process]$Process)
    if (!$Process -or $Process.HasExited) { return }
    try {
        $response = Invoke-HistoryRequest -Command "quit" -TimeoutMs 2000
        if (!$response.ok) { throw "quit rejected" }
        if (!$Process.WaitForExit(10000)) { throw "quit timed out" }
    } catch {
        if (!$Process.HasExited) {
            Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue
            $Process.WaitForExit(5000) | Out-Null
        }
        throw
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Name, $Actual, $Expected)
    if (!$Condition) {
        throw "FAIL $Name actual=$($Actual | ConvertTo-Json -Compress -Depth 6) expected=$($Expected | ConvertTo-Json -Compress -Depth 6)"
    }
    Write-Host "PASS $Name"
}

try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    $resolvedTemporaryRoot = [IO.Path]::GetFullPath($temporaryRoot)
    foreach ($candidate in @($settingsFile, $historyFile)) {
        if (![IO.Path]::GetFullPath($candidate).StartsWith(
                $resolvedTemporaryRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "隔离 settings/history 路径越界：$candidate"
        }
    }
    foreach ($required in @($executable, $testExecutable)) {
        if (!(Test-Path -LiteralPath $required)) { throw "找不到测试输入：$required" }
    }

    & $testExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "ScratchEditorClipboardHistoryTests 失败，退出码 $LASTEXITCODE"
    }

    $env:SCRATCHEDITOR_SERVER_NAME = $ServerName
    $env:SCRATCHEDITOR_SETTINGS_FILE = $settingsFile
    $process = Start-IsolatedEditor
    $initial = Invoke-HistoryRequest -Command "status"
    Assert-True ($initial.historyAvailable -eq $true) "history available" $initial.historyAvailable $true
    Assert-True ([IO.Path]::GetFullPath($initial.historyStoreFile) -eq [IO.Path]::GetFullPath($historyFile)) "isolated history path" $initial.historyStoreFile $historyFile

    Invoke-HistoryRequest -Command "testResetClipboardHistory" | Out-Null
    Invoke-HistoryRequest -Command "testSetClipboard" -Arguments @{ text = "virtual-current" } | Out-Null
    $afterSet = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    Assert-True ($afterSet.items.Count -eq 0) "testSetClipboard does not capture" $afterSet.items.Count 0

    Invoke-HistoryRequest -Command "testSetClipboardHistoryFault" -Arguments @{
        operation = "listenerRegistration"; enabled = $true
    } | Out-Null
    $listenerFailed = Invoke-HistoryRequest -Command "testRestartClipboardMonitoring"
    Assert-True (!$listenerFailed.ok) "listener registration failure is observable" $listenerFailed $false
    Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "listener-error-survival"
    } | Out-Null
    Invoke-HistoryRequest -Command "testWaitForClipboardHistoryIdle" -Arguments @{ timeoutMs = 10000 } | Out-Null
    $listenerErrorAfterSave = Invoke-HistoryRequest -Command "status"
    Assert-True (!$listenerErrorAfterSave.historyHealthy -and
                 ![string]::IsNullOrWhiteSpace($listenerErrorAfterSave.historyError)) `
        "store success does not clear listener error" $listenerErrorAfterSave.historyError "listener error"
    Invoke-HistoryRequest -Command "testSetClipboardHistoryFault" -Arguments @{
        operation = "listenerRegistration"; enabled = $false
    } | Out-Null
    $listenerRecovered = Invoke-HistoryRequest -Command "testRestartClipboardMonitoring"
    Assert-True ($listenerRecovered.ok) "listener registration recovers" $listenerRecovered.ok $true
    Invoke-HistoryRequest -Command "testResetClipboardHistory" | Out-Null

    Invoke-HistoryRequest -Command "testSetClipboardHistoryFault" -Arguments @{
        operation = "sequenceRace"; enabled = $true
    } | Out-Null
    $sequenceRace = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "sequence-race-retry"; processThroughMonitor = $true
    }
    Assert-True ($sequenceRace.captured -and $sequenceRace.outcome -eq "inserted") `
        "sequence race retries through monitoring path" $sequenceRace "captured inserted"
    Invoke-HistoryRequest -Command "testResetClipboardHistory" | Out-Null

    Invoke-HistoryRequest -Command "testSetClipboard" -Arguments @{ text = "self-write-seed" } | Out-Null
    Invoke-HistoryRequest -Command "show" | Out-Null
    Invoke-HistoryRequest -Command "testSetText" -Arguments @{ text = "self-write-once" } | Out-Null
    Invoke-HistoryRequest -Command "hide" | Out-Null
    Start-Sleep -Milliseconds 250
    $beforeSelfNotification = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    $selfSequence = [int64]$beforeSelfNotification.testSelfWriteSequence
    $selfNotification = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "self-write-once"; sequenceNumber = $selfSequence
    }
    $afterSelfNotification = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    Assert-True ($selfSequence -gt 0 -and $selfNotification.outcome -eq "selfWriteNotification" -and
                 $afterSelfNotification.revision -eq $beforeSelfNotification.revision -and
                 (($afterSelfNotification.items | ConvertTo-Json -Compress -Depth 6) -eq
                  ($beforeSelfNotification.items | ConvertTo-Json -Compress -Depth 6))) `
        "self write notification is suppressed exactly once" $selfNotification "selfWriteNotification"

    Invoke-HistoryRequest -Command "testSetClipboardHistoryFault" -Arguments @{
        operation = "write"; enabled = $true
    } | Out-Null
    Invoke-HistoryRequest -Command "testSetClipboard" -Arguments @{ text = "failed-write-seed" } | Out-Null
    Invoke-HistoryRequest -Command "show" | Out-Null
    Invoke-HistoryRequest -Command "testSetText" -Arguments @{ text = "must-not-record-failed-write" } | Out-Null
    Invoke-HistoryRequest -Command "hide" | Out-Null
    $afterFailedWrite = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    Invoke-HistoryRequest -Command "testSetClipboardHistoryFault" -Arguments @{
        operation = "write"; enabled = $false
    } | Out-Null
    Invoke-HistoryRequest -Command "testDiscardClose" | Out-Null
    Invoke-HistoryRequest -Command "testSetClipboard" -Arguments @{ text = "discard-write-seed" } | Out-Null
    Invoke-HistoryRequest -Command "show" | Out-Null
    Invoke-HistoryRequest -Command "testSetText" -Arguments @{ text = "must-not-record-discard" } | Out-Null
    Invoke-HistoryRequest -Command "testDiscardClose" | Out-Null
    $afterDiscard = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    Assert-True ($afterFailedWrite.revision -eq $afterSelfNotification.revision -and
                 $afterDiscard.revision -eq $afterSelfNotification.revision) `
        "failed commit and discard do not record history" `
        @{ failed = $afterFailedWrite.revision; discard = $afterDiscard.revision } `
        $afterSelfNotification.revision
    Invoke-HistoryRequest -Command "testResetClipboardHistory" | Out-Null

    foreach ($ignored in @(
        @{ kind = "empty"; expected = "empty" },
        @{ kind = "nonText"; expected = "nonText" },
        @{ kind = "readFailure"; expected = "readFailure" },
        @{ kind = "text"; text = "malformed-format"; historyFormat = "malformed"; expected = "excludedFromHistory" }
    )) {
        $arguments = @{} + $ignored
        $expectedOutcome = $arguments.expected
        $arguments.Remove("expected")
        $ignoredResult = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments $arguments
        Assert-True ($ignoredResult.outcome -eq $expectedOutcome) "ignored candidate $expectedOutcome" $ignoredResult.outcome $expectedOutcome
    }

    $visibleTimer = [Diagnostics.Stopwatch]::StartNew()
    $visibleCapture = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "capture-latency-probe"
    }
    $visibleTimer.Stop()
    Assert-True ($visibleCapture.captured -and $visibleTimer.Elapsed.TotalMilliseconds -le 500) `
        "capture visible within 500ms" $visibleTimer.Elapsed.TotalMilliseconds 500
    Invoke-HistoryRequest -Command "testSetClipboard" -Arguments @{ text = "reset-preserves-current" } | Out-Null
    Invoke-HistoryRequest -Command "testResetClipboardHistory" | Out-Null
    $clipboardAfterReset = Invoke-HistoryRequest -Command "testClipboard"
    Assert-True ($clipboardAfterReset.text -eq "reset-preserves-current") `
        "test reset preserves virtual clipboard" $clipboardAfterReset.text "reset-preserves-current"

    $captured = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "history-secret-你好"; sequenceNumber = 42; capturedAtMs = 1786200000000
    }
    Assert-True ($captured.outcome -eq "inserted") "explicit capture" $captured.outcome "inserted"
    $excluded = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "excluded"; excludeFromMonitor = $true
    }
    Assert-True ($excluded.outcome -eq "excludedFromMonitor") "monitor exclusion" $excluded.outcome "excludedFromMonitor"
    $historyExcluded = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "history-excluded"; excludeFromHistory = $true
    }
    Assert-True ($historyExcluded.outcome -eq "excludedFromHistory") "history exclusion" $historyExcluded.outcome "excludedFromHistory"
    $idle = Invoke-HistoryRequest -Command "testWaitForClipboardHistoryIdle" -Arguments @{ timeoutMs = 10000 }
    Assert-True ($idle.ok -eq $true) "history persisted" $idle.ok $true
    $state = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    $stableId = $state.items[0].id
    Assert-True ($state.items.Count -eq 1 -and $state.items[0].text -eq "history-secret-你好") "history state" $state.items "one exact item"
    $cipherAsUtf8 = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($historyFile))
    Assert-True (!$cipherAsUtf8.Contains("history-secret-你好")) "cipher excludes plaintext" "cipher" "no plaintext"

    Stop-IsolatedEditor $process
    for ($cycle = 1; $cycle -le 10; ++$cycle) {
        $process = Start-IsolatedEditor
        $restored = Invoke-HistoryRequest -Command "testClipboardHistoryState"
        Assert-True ($restored.items.Count -eq 1 -and $restored.items[0].id -eq $stableId -and
                     $restored.items[0].text -eq "history-secret-你好") "restart recovery $cycle" $restored.items "stable item"
        Stop-IsolatedEditor $process
    }

    $process = Start-IsolatedEditor
    $deleteCapture = Invoke-HistoryRequest -Command "testEmitClipboardChange" -Arguments @{
        kind = "text"; text = "delete-and-restart"
    }
    Assert-True ($deleteCapture.captured) "delete fixture captured" $deleteCapture.outcome "inserted"
    $deleteId = (Invoke-HistoryRequest -Command "testClipboardHistoryState").items[0].id
    Invoke-HistoryRequest -Command "show" | Out-Null
    Invoke-HistoryRequest -Command "testExecuteCommand" -Arguments @{ commandId = "clipboardHistory" } | Out-Null
    Invoke-HistoryRequest -Command "testClipboardHistoryUiAction" -Arguments @{
        action = "historySelect"; value = $deleteId
    } | Out-Null
    Invoke-HistoryRequest -Command "testClipboardHistoryUiAction" -Arguments @{
        action = "historyDeleteSelected"
    } | Out-Null
    Invoke-HistoryRequest -Command "testWaitForClipboardHistoryIdle" -Arguments @{ timeoutMs = 10000 } | Out-Null
    Stop-IsolatedEditor $process
    $process = Start-IsolatedEditor
    $afterDeleteRestart = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    Assert-True (!(($afterDeleteRestart.items | ForEach-Object { $_.id }) -contains $deleteId)) `
        "deleted item stays deleted after restart" $afterDeleteRestart.items $deleteId
    Stop-IsolatedEditor $process

    $lastKnownGoodHash = (Get-FileHash -LiteralPath $historyFile -Algorithm SHA256).Hash
    $bytes = [IO.File]::ReadAllBytes($historyFile)
    $bytes[[Math]::Floor($bytes.Length / 2)] = $bytes[[Math]::Floor($bytes.Length / 2)] -bxor 1
    [IO.File]::WriteAllBytes($historyFile, $bytes)
    $corruptHash = (Get-FileHash -LiteralPath $historyFile -Algorithm SHA256).Hash
    $process = Start-IsolatedEditor
    $locked = Invoke-HistoryRequest -Command "status"
    Assert-True ($locked.historyStoreState -eq "ReadLocked") "corrupt store locks writes" $locked.historyStoreState "ReadLocked"
    Assert-True ((Get-FileHash -LiteralPath $historyFile -Algorithm SHA256).Hash -eq $corruptHash) `
        "corrupt file is not automatically overwritten" (Get-FileHash $historyFile).Hash $corruptHash
    Invoke-HistoryRequest -Command "testSetClipboard" -Arguments @{ text = "clear-preserves-current" } | Out-Null
    Invoke-HistoryRequest -Command "show" | Out-Null
    Invoke-HistoryRequest -Command "testExecuteCommand" -Arguments @{ commandId = "clipboardHistory" } | Out-Null
    Invoke-HistoryRequest -Command "testClipboardHistoryUiAction" -Arguments @{
        action = "historyRequestClear"
    } | Out-Null
    Invoke-HistoryRequest -Command "testClipboardHistoryUiAction" -Arguments @{
        action = "historyConfirmClear"
    } | Out-Null
    $clearIdle = Invoke-HistoryRequest -Command "testWaitForClipboardHistoryIdle" -Arguments @{ timeoutMs = 10000 }
    $clipboardAfterClear = Invoke-HistoryRequest -Command "testClipboard"
    Assert-True ($clearIdle.ok -and $clipboardAfterClear.text -eq "clear-preserves-current") `
        "explicit corrupt reset persists empty without clipboard mutation" `
        @{ idle = $clearIdle.ok; clipboard = $clipboardAfterClear.text } `
        @{ idle = $true; clipboard = "clear-preserves-current" }
    Stop-IsolatedEditor $process
    Assert-True ((Get-FileHash -LiteralPath $historyFile -Algorithm SHA256).Hash -ne $corruptHash) `
        "corrupt file changes only after explicit clear" (Get-FileHash $historyFile).Hash "not $corruptHash"
    $process = Start-IsolatedEditor
    $afterCorruptReset = Invoke-HistoryRequest -Command "testClipboardHistoryState"
    Assert-True ($afterCorruptReset.items.Count -eq 0 -and $afterCorruptReset.historyStoreState -eq "Ready") `
        "explicit corrupt reset survives restart" $afterCorruptReset "empty Ready"
    Stop-IsolatedEditor $process
    Write-Host "Clipboard history isolated validation passed; last-known-good before deliberate corruption: $lastKnownGoodHash"
} finally {
    foreach ($owned in $startedProcesses) {
        if ($owned -and !$owned.HasExited) {
            Stop-Process -Id $owned.Id -ErrorAction SilentlyContinue
            $owned.WaitForExit(5000) | Out-Null
        }
    }
    $env:SCRATCHEDITOR_SERVER_NAME = $previousServer
    $env:SCRATCHEDITOR_SETTINGS_FILE = $previousSettings
    if (Test-Path -LiteralPath $temporaryRoot) {
        $resolved = [IO.Path]::GetFullPath($temporaryRoot)
        $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if ($resolved.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolved).StartsWith("ScratchEditor-ClipboardHistory-")) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
