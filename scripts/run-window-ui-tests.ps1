[CmdletBinding()]
param(
    [string]$BuildSubdirectory = "build\window-ui",
    [string]$ServerName = "ScratchEditor.WindowUi.Validation",
    [string]$OriginalAhkPath = "",
    [string]$ArtifactPrefix = "window-ui-results"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot $BuildSubdirectory
$editorExe = Join-Path $buildDir "ScratchEditor.exe"
$windowUiExe = Join-Path $buildDir "ScratchEditorWindowUiTests.exe"
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = $env:SCRATCHEDITOR_ORIGINAL_AHK
}
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = Join-Path $projectRoot "integration\KeysRedirect.QtMigration.ahk"
}
$originalAhk = $OriginalAhkPath
$artifactDir = Join-Path $projectRoot "artifacts"
$qtBin = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64\bin"
$mingwBin = Join-Path $projectRoot ".tools\Qt\Tools\mingw1310_64\bin"
$settingsDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "ScratchEditor\tests"
$settingsStem = $ServerName -replace '[^A-Za-z0-9_.-]', '_'
$settingsFile = Join-Path $settingsDirectory "$settingsStem.ini"
$env:PATH = "$qtBin;$mingwBin;$env:PATH"
$env:SCRATCHEDITOR_SERVER_NAME = $ServerName
$env:SCRATCHEDITOR_SETTINGS_FILE = $settingsFile

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

function Invoke-Utf8Process {
    param([Parameter(Mandatory)] [string]$FileName)
    $encoding = [System.Text.UTF8Encoding]::new($false)
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FileName
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $encoding
    $info.StandardErrorEncoding = $encoding
    $process = [System.Diagnostics.Process]::Start($info)
    $outputTask = $process.StandardOutput.ReadToEndAsync()
    $errorTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = $outputTask.Result
        Error = $errorTask.Result
    }
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
        throw "The isolated window UI validation instance did not become ready."
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
    if (-not $Started.Process.WaitForExit(2000)) {
        throw "Isolated process $($Started.Process.Id) did not exit."
    }
}

function Get-ProtectedProcessIds {
    @(
        Get-Process ScratchEditor -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -ne $editorExe } |
            ForEach-Object { $_.Id }
    )
}

foreach ($required in @($editorExe, $windowUiExe, $originalAhk)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required window UI validation input is missing: $required"
    }
}

New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
New-Item -ItemType Directory -Path $settingsDirectory -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$hashBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
$repoStatusBefore = (& git -C (Split-Path -Parent $originalAhk) status --short --branch) -join "`n"
$protectedBefore = Get-ProtectedProcessIds
$started = $null
$restarted = $null

try {
    Stop-IsolatedInstance
    $legacySettingsText = @'
[meta]
schemaVersion=1
[appearance]
theme=dark
[editor]
fontFamily=Microsoft YaHei UI
fontPointSize=13
[ui]
animationsEnabled=true
[statusPanel]
fontSize=10
showDelayMs=300
hideDelayMs=250
maxWidth=360
'@
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($settingsFile, $legacySettingsText, $utf8WithoutBom)
    $started = Start-IsolatedInstance
    $migrated = Send-IpcCommand -Command "status"
    $migratedText = Get-Content -LiteralPath $settingsFile -Raw -Encoding UTF8
    $migrationPassed = (
        [int]$migrated.settingsSchemaVersion -eq 4 -and
        $migrated.editorFontFamily -eq "Microsoft YaHei UI" -and
        $migrated.editorFallbackFontFamily -eq "NSimSun" -and
        [int]$migrated.editorFontWeight -eq 400 -and
        $migratedText.Contains("[appearance]") -and
        $migratedText.Contains("fontFamily=Microsoft YaHei UI") -and
        $migratedText.Contains("fontPointSize=13") -and
        $migratedText.Contains("fontWeight=400") -and
        $migratedText.Contains("animationsEnabled=true") -and
        -not $migratedText.Contains("[editor]") -and
        -not $migratedText.Contains("[ui]")
    )
    $reset = Send-IpcCommand -Command "testResetSettings"
    $initial = Send-IpcCommand -Command "status"
    $expectedGeometry = [ordered]@{
        x = [int]$initial.x + 24
        y = [int]$initial.y + 18
        width = 800
        height = 560
    }
    $geometrySet = Send-IpcRequest -Request @{
        command = "testSetGeometry"
        x = $expectedGeometry.x
        y = $expectedGeometry.y
        width = $expectedGeometry.width
        height = $expectedGeometry.height
    }

    $windowUiRun = Invoke-Utf8Process -FileName $windowUiExe
    $windowUiBehavior = $windowUiRun.Output | ConvertFrom-Json

    Stop-StartedInstance -Started $started
    $started = $null
    $restarted = Start-IsolatedInstance
    $persisted = Send-IpcCommand -Command "status"
    $persistedShortcut = Send-IpcRequest -Request @{
        command = "testShortcut"
        commandId = "toggleBold"
    }
    $configText = Get-Content -LiteralPath $settingsFile -Raw -Encoding UTF8
    $persistencePassed = (
        $persisted.theme -eq "light" -and
        $persisted.editorFontFamily -eq "Consolas" -and
        $persisted.editorFallbackFontFamily -eq "NSimSun" -and
        [int]$persisted.editorFontPointSize -eq 15 -and
        [int]$persisted.editorFontWeight -eq 600 -and
        -not [bool]$persisted.animationsEnabled -and
        $persistedShortcut.shortcut -eq "Ctrl+Alt+B" -and
        [int]$persisted.x -eq $expectedGeometry.x -and
        [int]$persisted.y -eq $expectedGeometry.y -and
        [int]$persisted.width -eq $expectedGeometry.width -and
        [int]$persisted.height -eq $expectedGeometry.height
    )
    $centralFilePassed = (
        (Test-Path -LiteralPath $settingsFile) -and
        $persisted.settingsFile -eq $settingsFile -and
        $configText.Contains("[appearance]") -and
        $configText.Contains("[shortcuts]") -and
        $configText.Contains("[statusPanel]") -and
        $configText.Contains("[window]") -and
        -not $configText.Contains("[editor]") -and
        -not $configText.Contains("[ui]")
    )
    $null = Send-IpcCommand -Command "testResetSettings"

    $sourceFiles = @(
        Get-ChildItem -LiteralPath (Join-Path $projectRoot "src"), (Join-Path $projectRoot "qml") `
            -File -Recurse
    )
    $browserReferences = @(
        $sourceFiles | Select-String -Pattern "WebEngine|WebView|Chromium" -CaseSensitive:$false
    )
    $hashAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $originalAhk).Hash
    $repoStatusAfter = (& git -C (Split-Path -Parent $originalAhk) status --short --branch) -join "`n"
    $protectedAfter = Get-ProtectedProcessIds
    $protectedPreserved = -not (@($protectedBefore | Where-Object {
        $protectedAfter -notcontains $_
    }).Count)

    $checks = [ordered]@{
        windowUiBehavior = ($windowUiRun.ExitCode -eq 0 -and [bool]$windowUiBehavior.allPassed)
        appearanceAndGeometryPersistence = $persistencePassed
        settingsSchemaMigration = $migrationPassed
        centralizedIniConfiguration = $centralFilePassed
        lazySettingsPage = [bool]$windowUiBehavior.checks.lazySettingsPage
        deferredFeaturesExcluded = [bool]$windowUiBehavior.checks.deferredFeaturesExcluded
        browserEngineExcluded = ($browserReferences.Count -eq 0)
        originalAhkUnchanged = ($hashBefore -eq $hashAfter)
        originalAhkRepoUnchanged = ($repoStatusBefore -eq $repoStatusAfter)
        runningUserInstancesPreserved = $protectedPreserved
    }
    $allPassed = -not ($checks.Values -contains $false)
    $report = [ordered]@{
        timestamp = (Get-Date).ToString("o")
        isolatedBuild = $buildDir
        isolatedServer = $ServerName
        reset = $reset
        expectedGeometry = $expectedGeometry
        geometrySet = $geometrySet
        windowUi = $windowUiBehavior
        windowUiStderr = $windowUiRun.Error
        persistedStatus = $persisted
        persistedShortcut = $persistedShortcut
        configuration = [ordered]@{
            file = $settingsFile
            schemaVersion = $persisted.settingsSchemaVersion
            centralized = $centralFilePassed
        }
        sourceProtection = [ordered]@{
            original = $originalAhk
            hashBefore = $hashBefore
            hashAfter = $hashAfter
            repoStatusBefore = $repoStatusBefore
            repoStatusAfter = $repoStatusAfter
        }
        userInstances = [ordered]@{
            idsBefore = $protectedBefore
            idsAfter = $protectedAfter
            preserved = $protectedPreserved
        }
        checks = $checks
        allPassed = $allPassed
    }
    $artifactPath = Join-Path $artifactDir "$ArtifactPrefix-$timestamp.json"
    [System.IO.File]::WriteAllText(
        $artifactPath,
        ($report | ConvertTo-Json -Depth 16),
        [System.Text.UTF8Encoding]::new($false)
    )
    [pscustomobject]@{
        allPassed = $allPassed
        checks = $checks
        artifact = $artifactPath
    } | ConvertTo-Json -Depth 6

    if (-not $allPassed) {
        exit 1
    }
}
finally {
    if ($null -ne $restarted) {
        Stop-StartedInstance -Started $restarted
    }
    elseif ($null -ne $started) {
        Stop-StartedInstance -Started $started
    }
}
