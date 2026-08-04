[CmdletBinding()]
param(
    [string]$OriginalAhkPath = "D:\Documents\AutoHotkey\KeysRedirect.ahk",
    [string]$BackupPath =
        "D:\Documents\AutoHotkey\KeysRedirect.ahk.stage6-backup-20260802-132834",
    [string]$BuildSubdirectory = "build\release",
    [string]$AutoHotkeyExecutable =
        "C:\Program Files\AutoHotkey\v2\AutoHotkey64.exe",
    [string]$ArtifactPrefix = "ahk-results"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$editorExe = Join-Path $projectRoot "$BuildSubdirectory\ScratchEditor.exe"
$prepareScript = Join-Path $PSScriptRoot "prepare-ahk-test.ps1"
$expectedBackupHash =
    "8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9"

foreach ($path in @(
        $OriginalAhkPath, $BackupPath, $editorExe, $AutoHotkeyExecutable, $prepareScript
    )) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required AHK migration file not found: $path"
    }
}

$original = (Resolve-Path -LiteralPath $OriginalAhkPath).Path
$backup = (Resolve-Path -LiteralPath $BackupPath).Path
$editor = (Resolve-Path -LiteralPath $editorExe).Path
$ahk = (Resolve-Path -LiteralPath $AutoHotkeyExecutable).Path
$originalHash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
$backupHash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash
$originalContent = [System.IO.File]::ReadAllText($original, [System.Text.Encoding]::UTF8)
$backupContent = [System.IO.File]::ReadAllText($backup, [System.Text.Encoding]::UTF8)

$tempDirectory = Join-Path $env:TEMP (
    "ScratchEditor-Ahk\validation-" + [guid]::NewGuid().ToString("N")
)
New-Item -ItemType Directory -Path $tempDirectory -Force | Out-Null
$generatedCandidate = Join-Path $tempDirectory "KeysRedirect.Generated.ahk"
$installedTestCopy = Join-Path $tempDirectory "KeysRedirect.InstalledTest.ahk"

function Get-ProcessIds {
    param([Parameter(Mandatory)][string]$Name)
    return @(
        Get-Process -Name $Name -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Id
    )
}

function Invoke-AhkTestMode {
    param(
        [Parameter(Mandatory)][string]$Mode,
        [Parameter(Mandatory)][string]$ServerName,
        [Parameter(Mandatory)][string]$ExecutablePath,
        [Parameter(Mandatory)][string]$ExpectedOutput
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ahk
    $startInfo.Arguments = '"' + $installedTestCopy +
        '" --scratch-editor-ahk-test ' + $Mode
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_AHK_TEST"] = "1"
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_SERVER_NAME"] = $ServerName
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_EXE"] = $ExecutablePath
    $startInfo.EnvironmentVariables["SCRATCHEDITOR_SETTINGS_FILE"] =
        (Join-Path $tempDirectory "$ServerName.ini")

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(10000)) {
        throw "AHK $Mode test did not exit; PID $($process.Id)."
    }
    $stdout = $stdoutTask.Result.Trim()
    $stderr = $stderrTask.Result.Trim()
    return [ordered]@{
        mode = $Mode
        serverName = $ServerName
        exitCode = $process.ExitCode
        stdout = $stdout
        stderr = $stderr
        passed = (
            $process.ExitCode -eq 0 -and
            $stdout -eq $ExpectedOutput -and
            [string]::IsNullOrWhiteSpace($stderr)
        )
    }
}

$ahkBefore = Get-ProcessIds -Name "AutoHotkey64"
$editorBefore = Get-ProcessIds -Name "ScratchEditor"
$fallbackResult = $null
$ipcResult = $null

try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $prepareScript `
        -SourcePath $backup `
        -OutputPath $generatedCandidate `
        -ScratchEditorExecutable $editor `
        -ExpectedSourceSha256 $expectedBackupHash | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "AHK test candidate regeneration failed with exit code $LASTEXITCODE."
    }
    Copy-Item -LiteralPath $original -Destination $installedTestCopy

    $fallbackServer = "ScratchEditor.Ahk.Fallback." +
        [guid]::NewGuid().ToString("N")
    $fallbackResult = Invoke-AhkTestMode `
        -Mode "fallback" `
        -ServerName $fallbackServer `
        -ExecutablePath "C:\Path-That-Does-Not-Exist\ScratchEditor.exe" `
        -ExpectedOutput "ahk-fallback-pass"

    $ipcServer = "ScratchEditor.Ahk.Ipc." + [guid]::NewGuid().ToString("N")
    $ipcResult = Invoke-AhkTestMode `
        -Mode "ipc" `
        -ServerName $ipcServer `
        -ExecutablePath $editor `
        -ExpectedOutput "ahk-ipc-pass"

    for ($attempt = 0; $attempt -lt 120; $attempt++) {
        $currentEditors = Get-ProcessIds -Name "ScratchEditor"
        $extraEditors = @($currentEditors | Where-Object { $editorBefore -notcontains $_ })
        if ($extraEditors.Count -eq 0) {
            break
        }
        Start-Sleep -Milliseconds 25
    }
}
finally {
    $ahkAfter = Get-ProcessIds -Name "AutoHotkey64"
    $editorAfter = Get-ProcessIds -Name "ScratchEditor"
}

$generatedHash = (Get-FileHash -LiteralPath $generatedCandidate -Algorithm SHA256).Hash
$testCopyHash = (Get-FileHash -LiteralPath $installedTestCopy -Algorithm SHA256).Hash
$legacyPatterns = @(
    "ScratchGui", "ScratchVisible", "Gui(", "CloseScratchOnExit",
    "ScratchHitTest", "TryNativeDrag", "ApplyWin11WindowStyle"
)
$legacyReferences = @($legacyPatterns | Where-Object { $originalContent.Contains($_) })
$fallbackStart = $originalContent.IndexOf("ScratchEditorClipboardFallback() {")
$fallbackSection = if ($fallbackStart -ge 0) {
    $originalContent.Substring($fallbackStart)
}
else {
    ""
}

$externalRepository = Split-Path -Parent $original
$externalStatus = @(& git -C $externalRepository status --porcelain=v1)
$expectedBackupName = Split-Path -Leaf $backup
$expectedExternalEntries = @(
    " M KeysRedirect.ahk",
    "?? $expectedBackupName"
)
$externalScopeClean = (
    $externalStatus.Count -eq $expectedExternalEntries.Count -and
    @($externalStatus | Where-Object { $expectedExternalEntries -notcontains $_ }).Count -eq 0
)

$extraAhk = @($ahkAfter | Where-Object { $ahkBefore -notcontains $_ })
$extraEditors = @($editorAfter | Where-Object { $editorBefore -notcontains $_ })
$checks = [ordered]@{
    backupExists = (Test-Path -LiteralPath $backup -PathType Leaf)
    backupSharesOriginalDirectory = (
        (Split-Path -Parent $backup) -eq (Split-Path -Parent $original)
    )
    backupHashMatchesBaseline = ($backupHash -eq $expectedBackupHash)
    installedMatchesControlledTransformation = (
        $originalHash -eq $generatedHash -and $testCopyHash -eq $originalHash
    )
    legacyAhkGuiRemoved = ($legacyReferences.Count -eq 0)
    legacyBackupRetained = (
        $backupContent.Contains("global ScratchGui") -and
        $backupContent.Contains('ScratchGui := Gui(')
    )
    shortcutPreserved = (
        $originalContent.Contains("ScrollLock::ToggleScratchEditor()") -and
        $originalContent.Contains("ScrollLock::F14")
    )
    ipcPreserved = (
        $originalContent.Contains("EnsureQtScratchEditorAndSend") -and
        $originalContent.Contains("SendQtScratchEditorCommand") -and
        $originalContent.Contains("IsQtScratchEditorReady")
    )
    residentStartupPreserved = (
        $originalContent.Contains("SetTimer StartQtScratchEditorResident, -1")
    )
    clipboardFallbackPreserved = (
        $fallbackSection.Contains("ScratchEditorClipboardFallback()") -and
        $fallbackSection.Contains("剪贴板内容未更改") -and
        -not $fallbackSection.Contains("A_Clipboard :=")
    )
    configuredExecutableExists = (Test-Path -LiteralPath $editor -PathType Leaf)
    fallbackTest = [bool]$fallbackResult.passed
    persistentIpcTest = [bool]$ipcResult.passed
    userAhkProcessesPreserved = (
        @($ahkBefore | Where-Object { $ahkAfter -contains $_ }).Count -eq $ahkBefore.Count
    )
    userEditorProcessesPreserved = (
        @($editorBefore | Where-Object { $editorAfter -contains $_ }).Count -eq
            $editorBefore.Count
    )
    noTestProcessesRemain = ($extraAhk.Count -eq 0 -and $extraEditors.Count -eq 0)
    externalRepositoryScope = $externalScopeClean
}
$allPassed = -not ($checks.Values -contains $false)
$report = [ordered]@{
    timestamp = (Get-Date).ToString("o")
    original = [ordered]@{
        path = $original
        sha256 = $originalHash
        length = (Get-Item -LiteralPath $original).Length
    }
    backup = [ordered]@{
        path = $backup
        sha256 = $backupHash
        length = (Get-Item -LiteralPath $backup).Length
    }
    editor = [ordered]@{
        path = $editor
        sha256 = (Get-FileHash -LiteralPath $editor -Algorithm SHA256).Hash
    }
    tests = [ordered]@{
        fallback = $fallbackResult
        ipc = $ipcResult
    }
    processes = [ordered]@{
        ahkBefore = $ahkBefore
        ahkAfter = $ahkAfter
        editorBefore = $editorBefore
        editorAfter = $editorAfter
        extraAhk = $extraAhk
        extraEditors = $extraEditors
    }
    details = [ordered]@{
        generatedHash = $generatedHash
        installedTestCopyHash = $testCopyHash
        legacyReferences = $legacyReferences
        externalRepositoryStatus = $externalStatus
    }
    checks = $checks
    allPassed = $allPassed
}

$artifactDirectory = Join-Path $projectRoot "artifacts"
New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
$artifactPath = Join-Path $artifactDirectory (
    "$ArtifactPrefix-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".json"
)
$json = $report | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText(
    $artifactPath, $json, [System.Text.UTF8Encoding]::new($false)
)
$json
Write-Host "AHK report: $artifactPath"
if (-not $allPassed) {
    exit 1
}
