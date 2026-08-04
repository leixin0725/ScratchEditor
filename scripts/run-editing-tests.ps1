[CmdletBinding()]
param(
    [string]$BuildSubdirectory = "build\editing",
    [string]$ServerName = "ScratchEditor.Editing.Validation",
    [string]$OriginalAhkPath = ""
)

$ErrorActionPreference = "Stop"
$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot $BuildSubdirectory
$editorExe = Join-Path $buildDir "ScratchEditor.exe"
$editingExe = Join-Path $buildDir "ScratchEditorEditingTests.exe"
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = $env:SCRATCHEDITOR_ORIGINAL_AHK
}
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = Join-Path $projectRoot "dev-links\AutoHotkey\KeysRedirect.ahk"
}
$originalAhk = $OriginalAhkPath
$artifactDir = Join-Path $projectRoot "artifacts"
$qtBin = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64\bin"
$mingwBin = Join-Path $projectRoot ".tools\Qt\Tools\mingw1310_64\bin"
$releaseExe = Join-Path $projectRoot "build\release\ScratchEditor.exe"
$env:PATH = "$qtBin;$mingwBin;$env:PATH"
$env:SCRATCHEDITOR_SERVER_NAME = $ServerName

function Send-IpcRequest {
    param([Parameter(Mandatory)] [hashtable]$Request, [int]$TimeoutMs = 3000)
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", $ServerName, [System.IO.Pipes.PipeDirection]::InOut
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

function Stop-IsolatedInstance {
    try {
        $status = Send-IpcCommand -Command "status" -TimeoutMs 100
        if (-not $status.testMode -or $status.serverName -ne $ServerName) {
            throw "Refusing to stop a non-isolated ScratchEditor instance."
        }
        $null = Send-IpcCommand -Command "quit" -TimeoutMs 500
        Start-Sleep -Milliseconds 100
    }
    catch [System.TimeoutException] {
    }
    catch [System.IO.IOException] {
    }
}

function Start-IsolatedInstance {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $editorExe
    $startInfo.Arguments = "--background --test-mode"
    $startInfo.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $status = $null
    for ($attempt = 0; $attempt -lt 300; $attempt++) {
        try {
            $candidate = Send-IpcCommand -Command "status" -TimeoutMs 20
            if ($candidate.ready -and [int]$candidate.pid -eq $process.Id) {
                $status = $candidate
                break
            }
        }
        catch {
        }
        Start-Sleep -Milliseconds 5
    }
    if ($null -eq $status) {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id
        }
        throw "The isolated editing validation instance did not become ready."
    }
    [pscustomobject]@{ Process = $process; Status = $status }
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

function Get-ReleaseProcessIds {
    @(
        Get-Process ScratchEditor -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -eq $releaseExe } |
            ForEach-Object { $_.Id }
    )
}

foreach ($required in @($editorExe, $editingExe, $originalAhk)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required editing validation input is missing: $required"
    }
}

New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$hashBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
$repoStatusBefore = (& git -C (Split-Path -Parent $originalAhk) status --short --branch) -join "`n"
$releaseProcessesBefore = Get-ReleaseProcessIds
$started = $null
$secondStarted = $null

try {
    Stop-IsolatedInstance
    $started = Start-IsolatedInstance
    $null = Send-IpcCommand -Command "testResetSettings"

    $initialGeometry = Send-IpcCommand -Command "status"
    $expectedGeometry = [ordered]@{
        x = [int]$initialGeometry.x + 30
        y = [int]$initialGeometry.y + 24
        width = 780
        height = 540
    }
    $geometrySet = Send-IpcRequest -Request @{
        command = "testSetGeometry"
        x = $expectedGeometry.x
        y = $expectedGeometry.y
        width = $expectedGeometry.width
        height = $expectedGeometry.height
    }
    Stop-StartedInstance -Started $started
    $started = $null

    $secondStarted = Start-IsolatedInstance
    $restoredGeometry = Send-IpcCommand -Command "status"
    $geometryPassed = (
        [int]$restoredGeometry.x -eq $expectedGeometry.x -and
        [int]$restoredGeometry.y -eq $expectedGeometry.y -and
        [int]$restoredGeometry.width -eq $expectedGeometry.width -and
        [int]$restoredGeometry.height -eq $expectedGeometry.height
    )

    $editingRaw = & $editingExe | Out-String
    $editingExitCode = $LASTEXITCODE
    $jsonStart = $editingRaw.IndexOf('{')
    $jsonEnd = $editingRaw.LastIndexOf('}')
    $editingJson = if ($jsonStart -ge 0 -and $jsonEnd -gt $jsonStart) {
        $editingRaw.Substring($jsonStart, $jsonEnd - $jsonStart + 1)
    } else {
        $editingRaw
    }
    $editingBehavior = $editingJson | ConvertFrom-Json

    Stop-StartedInstance -Started $secondStarted
    $secondStarted = $null
    $started = Start-IsolatedInstance
    $persistedShortcut = Send-IpcRequest -Request @{
        command = "testShortcut"
        commandId = "toggleBold"
    }
    $shortcutPersistencePassed = (
        $persistedShortcut.shortcut -eq "Ctrl+Alt+B" -and
        [int]$persistedShortcut.settingsStatus -eq 0
    )
    $null = Send-IpcCommand -Command "testResetSettings"

    $hashAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
    $repoStatusAfter = (& git -C (Split-Path -Parent $originalAhk) status --short --branch) -join "`n"
    $releaseProcessesAfter = Get-ReleaseProcessIds
    $releaseInstancePreserved = -not (@($releaseProcessesBefore | Where-Object {
        $releaseProcessesAfter -notcontains $_
    }).Count)

    $checks = [ordered]@{
        editingBehavior = ($editingExitCode -eq 0 -and [bool]$editingBehavior.allPassed)
        geometryPersistence = $geometryPassed
        shortcutPersistence = $shortcutPersistencePassed
        previewExcluded = [bool]$editingBehavior.checks.previewExcluded
        originalAhkUnchanged = ($hashBefore -eq $hashAfter)
        originalAhkRepoUnchanged = ($repoStatusBefore -eq $repoStatusAfter)
        runningReleaseInstancePreserved = $releaseInstancePreserved
    }
    $allPassed = -not ($checks.Values -contains $false)
    $report = [ordered]@{
        timestamp = (Get-Date).ToString("o")
        isolatedBuild = $buildDir
        isolatedServer = $ServerName
        geometry = [ordered]@{
            expected = $expectedGeometry
            setResponse = $geometrySet
            restored = $restoredGeometry
        }
        shortcutPersistence = $persistedShortcut
        editing = $editingBehavior
        sourceProtection = [ordered]@{
            original = $originalAhk
            hashBefore = $hashBefore
            hashAfter = $hashAfter
            repoStatusBefore = $repoStatusBefore
            repoStatusAfter = $repoStatusAfter
        }
        releaseInstance = [ordered]@{
            idsBefore = $releaseProcessesBefore
            idsAfter = $releaseProcessesAfter
            preserved = $releaseInstancePreserved
        }
        checks = $checks
        allPassed = $allPassed
    }
    $artifactPath = Join-Path $artifactDir "editing-results-$timestamp.json"
    [System.IO.File]::WriteAllText(
        $artifactPath,
        ($report | ConvertTo-Json -Depth 14),
        [System.Text.UTF8Encoding]::new($false)
    )
    [pscustomobject]@{
        allPassed = $allPassed
        checks = $checks
        artifact = $artifactPath
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
