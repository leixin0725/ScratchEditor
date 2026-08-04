[CmdletBinding()]
param(
    [ValidateSet("Install", "Check")]
    [string]$Action = "Install",
    [string]$SourceEditorPath,
    [string]$InstallDirectory,
    [string]$AhkInstallDirectory,
    [string]$AhkScriptPath = "D:\Documents\AutoHotkey\KeysRedirect.ahk"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    throw "LOCALAPPDATA is not available."
}
$installRoot = Join-Path $env:LOCALAPPDATA "ScratchEditor"
if ([string]::IsNullOrWhiteSpace($InstallDirectory)) {
    $InstallDirectory = Join-Path $installRoot "CodexEditor"
}
if ([string]::IsNullOrWhiteSpace($AhkInstallDirectory)) {
    $AhkInstallDirectory = Join-Path $installRoot "AhkEditor"
}

$resolvedInstallDirectory = [System.IO.Path]::GetFullPath($InstallDirectory)
$resolvedEditorPath = Join-Path $resolvedInstallDirectory "ScratchEditor.exe"
$resolvedAhkInstallDirectory = [System.IO.Path]::GetFullPath($AhkInstallDirectory)
$resolvedAhkEditorPath = Join-Path $resolvedAhkInstallDirectory "ScratchEditor.exe"
$resolvedAhkScriptPath = [System.IO.Path]::GetFullPath($AhkScriptPath)
if ([string]::IsNullOrWhiteSpace($SourceEditorPath)) {
    $SourceEditorPath = Join-Path $projectRoot "build\release\ScratchEditor.exe"
}
$resolvedSourceEditorPath = [System.IO.Path]::GetFullPath($SourceEditorPath)
$deployQt = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64\bin\windeployqt.exe"
$qmlDirectory = Join-Path $projectRoot "qml"
$markdownStyle = Join-Path $projectRoot "config\markdown-style.json"
$restartAhkResident = $false

function Invoke-ProductionEditorCommand {
    param([Parameter(Mandatory)] [string]$Command, [int]$TimeoutMs = 1000)

    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", "ScratchEditor.Stage1.v1", [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $pipe.Connect($TimeoutMs)
        $encoding = [System.Text.UTF8Encoding]::new($false)
        $writer = [System.IO.StreamWriter]::new($pipe, $encoding, 4096, $true)
        $reader = [System.IO.StreamReader]::new($pipe, $encoding, $false, 4096, $true)
        try {
            $payload = @{ command = $Command } | ConvertTo-Json -Compress
            $writer.WriteLine($payload)
            $writer.Flush()
            $line = $reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($line)) {
                return $null
            }
            return $line | ConvertFrom-Json
        }
        finally {
            $reader.Dispose()
            $writer.Dispose()
        }
    }
    catch [System.TimeoutException] {
        return $null
    }
    catch [System.IO.IOException] {
        return $null
    }
    catch [System.UnauthorizedAccessException] {
        return $null
    }
    finally {
        $pipe.Dispose()
    }
}

function Stop-StableAhkResident {
    $status = Invoke-ProductionEditorCommand -Command "status"
    if ($null -eq $status -or [string]::IsNullOrWhiteSpace($status.executableFile)) {
        return $false
    }
    $residentPath = [System.IO.Path]::GetFullPath([string]$status.executableFile)
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals(
            $residentPath, $resolvedAhkEditorPath)) {
        return $false
    }

    $null = Invoke-ProductionEditorCommand -Command "hide"
    $shutdown = Invoke-ProductionEditorCommand -Command "shutdownForUpdate"
    if ($null -eq $shutdown -or -not [bool]$shutdown.ok) {
        throw "The stable AHK ScratchEditor resident rejected the update shutdown request."
    }
    $residentPid = [int]$status.pid
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        Start-Sleep -Milliseconds 50
        if ($null -eq (Get-Process -Id $residentPid -ErrorAction SilentlyContinue)) {
            return $true
        }
    }
    throw "The stable AHK ScratchEditor resident did not stop itself for the update."
}

function Start-StableAhkResident {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolvedAhkEditorPath
    $startInfo.Arguments = "--background"
    $startInfo.UseShellExecute = $false
    $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $process = [System.Diagnostics.Process]::Start($startInfo)
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        Start-Sleep -Milliseconds 20
        $status = Invoke-ProductionEditorCommand -Command "status" -TimeoutMs 100
        if ($null -ne $status -and -not [string]::IsNullOrWhiteSpace($status.executableFile)) {
            $runningPath = [System.IO.Path]::GetFullPath([string]$status.executableFile)
            if ([StringComparer]::OrdinalIgnoreCase.Equals(
                    $runningPath, $resolvedAhkEditorPath)) {
                return
            }
        }
        if ($process.HasExited) {
            throw "The stable AHK ScratchEditor resident exited during startup."
        }
    }
    throw "The stable AHK ScratchEditor resident did not become ready."
}

function Install-EditorCopy {
    param(
        [Parameter(Mandatory)] [string]$TargetDirectory,
        [Parameter(Mandatory)] [string]$TargetEditorPath
    )

    New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
    Copy-Item -LiteralPath $resolvedSourceEditorPath -Destination $TargetEditorPath -Force
    $installedConfigDirectory = Join-Path $TargetDirectory "config"
    New-Item -ItemType Directory -Path $installedConfigDirectory -Force | Out-Null
    Copy-Item -LiteralPath $markdownStyle `
        -Destination (Join-Path $installedConfigDirectory "markdown-style.json") -Force

    $deployOptions = @(
        "--release",
        "--no-translations",
        "--no-system-d3d-compiler",
        "--no-system-dxc-compiler",
        "--no-opengl-sw",
        "--compiler-runtime",
        "--skip-plugin-types", "qmltooling,generic",
        "--qmldir", $qmlDirectory
    )
    & $deployQt @deployOptions $TargetEditorPath
    if ($LASTEXITCODE -ne 0) {
        throw "Qt deployment failed for $TargetEditorPath with exit code $LASTEXITCODE."
    }
}

if ($Action -eq "Install") {
    foreach ($sourcePath in @(
        $resolvedSourceEditorPath,
        $deployQt,
        $qmlDirectory,
        $markdownStyle
    )) {
        if (-not (Test-Path -LiteralPath $sourcePath)) {
            throw "Deployment source is missing: $sourcePath. Rebuild release before configuring Codex."
        }
    }
    $restartAhkResident = Stop-StableAhkResident
    try {
        Install-EditorCopy -TargetDirectory $resolvedInstallDirectory `
            -TargetEditorPath $resolvedEditorPath
        Install-EditorCopy -TargetDirectory $resolvedAhkInstallDirectory `
            -TargetEditorPath $resolvedAhkEditorPath
    }
    finally {
        if ($restartAhkResident) {
            Start-StableAhkResident
        }
    }
}

$requiredRuntimePaths = @(
    $resolvedEditorPath,
    (Join-Path $resolvedInstallDirectory "Qt6Core.dll"),
    (Join-Path $resolvedInstallDirectory "Qt6Gui.dll"),
    (Join-Path $resolvedInstallDirectory "Qt6Qml.dll"),
    (Join-Path $resolvedInstallDirectory "Qt6Quick.dll"),
    (Join-Path $resolvedInstallDirectory "platforms\qwindows.dll"),
    $resolvedAhkEditorPath,
    (Join-Path $resolvedAhkInstallDirectory "Qt6Core.dll"),
    (Join-Path $resolvedAhkInstallDirectory "Qt6Gui.dll"),
    (Join-Path $resolvedAhkInstallDirectory "Qt6Qml.dll"),
    (Join-Path $resolvedAhkInstallDirectory "Qt6Quick.dll"),
    (Join-Path $resolvedAhkInstallDirectory "platforms\qwindows.dll")
)

$missingRuntimePaths = @($requiredRuntimePaths | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($missingRuntimePaths.Count -gt 0) {
    $missingList = $missingRuntimePaths -join [Environment]::NewLine
    throw "The stable ScratchEditor installation is incomplete. Run this script with -Action Install. Missing:$([Environment]::NewLine)$missingList"
}

$quotedEditorPath = if ($resolvedEditorPath.Contains(" ")) {
    '"' + $resolvedEditorPath + '"'
} else {
    $resolvedEditorPath
}
$windowsCommand = "$quotedEditorPath --wait"
if ($resolvedEditorPath.Contains("'")) {
    throw "Editor paths containing a single quote are not supported in the Git Bash profile."
}
$bashEditorPath = $resolvedEditorPath.Replace("\", "/")
$bashCommand = "$bashEditorPath --wait"

$userProfileDirectory = $env:USERPROFILE
if ([string]::IsNullOrWhiteSpace($userProfileDirectory)) {
    throw "USERPROFILE is not available."
}
$bashProfilePath = Join-Path $userProfileDirectory ".bashrc"
$vscodeSettingsPath = Join-Path $env:APPDATA "Code\User\settings.json"
$piSettingsPath = Join-Path $userProfileDirectory ".pi\agent\settings.json"
$localInstallationDocument = Join-Path $projectRoot "docs\codex-editor-installation.local.md"
$ahkExpectedLine = '    QtScratchEditorExe := "' + $resolvedAhkEditorPath + '"'
$startMarker = "# >>> ScratchEditor Codex external editor >>>"
$endMarker = "# <<< ScratchEditor Codex external editor <<<"
$managedBlockLines = @(
    $startMarker,
    "# Managed by $($PSScriptRoot.Replace('\', '/'))/configure-codex-editor.ps1",
    'if [ "${TERM_PROGRAM:-}" = "vscode" ]; then',
    "    export VISUAL='code --wait'",
    'else',
    "    export VISUAL='$bashCommand'",
    'fi',
    'export EDITOR="$VISUAL"',
    $endMarker
)

function Get-BashProfileText {
    if (-not (Test-Path -LiteralPath $bashProfilePath -PathType Leaf)) {
        return ""
    }
    return [System.IO.File]::ReadAllText($bashProfilePath)
}

function Test-BashProfile {
    $profileText = Get-BashProfileText
    $expectedVisual = "export VISUAL='$bashCommand'"
    return $profileText.Contains($startMarker) `
        -and $profileText.Contains('if [ "${TERM_PROGRAM:-}" = "vscode" ]; then') `
        -and $profileText.Contains("export VISUAL='code --wait'") `
        -and $profileText.Contains($expectedVisual) `
        -and $profileText.Contains('export EDITOR="$VISUAL"')
}

function Write-BashProfile {
    $profileText = Get-BashProfileText
    $newline = if ($profileText.Contains("`r`n")) { "`r`n" } else { "`n" }
    $managedBlock = ($managedBlockLines -join $newline) + $newline
    $blockPattern = "(?ms)^" + [Regex]::Escape($startMarker) + ".*?^" `
        + [Regex]::Escape($endMarker) + "[ \t]*(?:\r?\n|$)"

    if ([Regex]::IsMatch($profileText, $blockPattern)) {
        $profileText = [Regex]::Replace($profileText, $blockPattern, $managedBlock)
    } else {
        foreach ($legacyLine in @(
            'export VISUAL="code --wait"',
            "export VISUAL='code --wait'",
            'export EDITOR="code --wait"',
            "export EDITOR='code --wait'"
        )) {
            $linePattern = "(?m)^[ \t]*" + [Regex]::Escape($legacyLine) `
                + "[ \t]*(?:\r?\n|$)"
            $profileText = [Regex]::Replace($profileText, $linePattern, "")
        }
        if ($profileText.Length -gt 0 -and -not $profileText.EndsWith("`n")) {
            $profileText += $newline
        }
        if ($profileText.Length -gt 0) {
            $profileText += $newline
        }
        $profileText += $managedBlock
    }

    $backupPath = "$bashProfilePath.scratcheditor.bak"
    if ((Test-Path -LiteralPath $bashProfilePath -PathType Leaf) `
        -and -not (Test-Path -LiteralPath $backupPath)) {
        Copy-Item -LiteralPath $bashProfilePath -Destination $backupPath
    }

    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($bashProfilePath, $profileText, $utf8WithoutBom)
}

function Test-VsCodeTerminalEnvironment {
    if (-not (Test-Path -LiteralPath $vscodeSettingsPath -PathType Leaf)) {
        return $false
    }
    $settingsText = [System.IO.File]::ReadAllText($vscodeSettingsPath)
    $propertyMatch = [Regex]::Match(
        $settingsText,
        '(?ms)"terminal\.integrated\.env\.windows"\s*:\s*\{(?<body>.*?)\}')
    if (-not $propertyMatch.Success) {
        return $false
    }
    $body = $propertyMatch.Groups['body'].Value
    return [Regex]::IsMatch($body, '"VISUAL"\s*:\s*"code --wait"') `
        -and [Regex]::IsMatch($body, '"EDITOR"\s*:\s*"code --wait"')
}

function Write-VsCodeTerminalEnvironment {
    if (Test-VsCodeTerminalEnvironment) {
        return
    }
    if (Test-Path -LiteralPath $vscodeSettingsPath -PathType Leaf) {
        $settingsText = [System.IO.File]::ReadAllText($vscodeSettingsPath)
        if ($settingsText -match '"terminal\.integrated\.env\.windows"\s*:') {
            throw "VS Code terminal.integrated.env.windows already exists with different VISUAL or EDITOR values."
        }
        $backupPath = "$vscodeSettingsPath.before-codex-editor.bak"
        if (-not (Test-Path -LiteralPath $backupPath)) {
            Copy-Item -LiteralPath $vscodeSettingsPath -Destination $backupPath
        }
    } else {
        $settingsDirectory = Split-Path -Parent $vscodeSettingsPath
        New-Item -ItemType Directory -Path $settingsDirectory -Force | Out-Null
        $settingsText = "{}"
    }

    $newline = if ($settingsText.Contains("`r`n")) { "`r`n" } else { "`n" }
    $propertyLines = @(
        '    "terminal.integrated.env.windows": {',
        '        "VISUAL": "code --wait",',
        '        "EDITOR": "code --wait"',
        '    },'
    )
    $propertyText = $newline + ($propertyLines -join $newline) + $newline
    $openingBrace = $settingsText.IndexOf('{')
    if ($openingBrace -lt 0) {
        throw "VS Code settings.json does not contain a root object."
    }
    $settingsText = $settingsText.Insert($openingBrace + 1, $propertyText)
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($vscodeSettingsPath, $settingsText, $utf8WithoutBom)
}

function Test-AhkEditorConfiguration {
    if (-not (Test-Path -LiteralPath $resolvedAhkScriptPath -PathType Leaf)) {
        return $true
    }
    $ahkText = [System.IO.File]::ReadAllText($resolvedAhkScriptPath)
    return $ahkText.Contains($ahkExpectedLine)
}

function Write-AhkEditorConfiguration {
    if (-not (Test-Path -LiteralPath $resolvedAhkScriptPath -PathType Leaf)) {
        return
    }
    $ahkText = [System.IO.File]::ReadAllText($resolvedAhkScriptPath)
    if ($ahkText.Contains($ahkExpectedLine)) {
        return
    }
    $assignmentPattern = '(?m)^[ \t]*QtScratchEditorExe\s*:=\s*"[^"\r\n]*ScratchEditor\.exe"[ \t]*$'
    $assignmentMatches = [Regex]::Matches($ahkText, $assignmentPattern)
    if ($assignmentMatches.Count -ne 1) {
        throw "Expected exactly one QtScratchEditorExe fallback assignment in $resolvedAhkScriptPath; found $($assignmentMatches.Count)."
    }

    $backupPath = "$resolvedAhkScriptPath.before-stable-install.bak"
    if (-not (Test-Path -LiteralPath $backupPath)) {
        Copy-Item -LiteralPath $resolvedAhkScriptPath -Destination $backupPath
    }
    $updatedText = [Regex]::Replace(
        $ahkText, $assignmentPattern,
        [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $ahkExpectedLine })
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($resolvedAhkScriptPath, $updatedText, $utf8WithoutBom)
}

function Test-PiEditorConfiguration {
    if (-not (Test-Path -LiteralPath $piSettingsPath -PathType Leaf)) {
        return $true
    }
    $settings = [System.IO.File]::ReadAllText($piSettingsPath) | ConvertFrom-Json
    return [string]$settings.externalEditor -eq $windowsCommand
}

function Write-PiEditorConfiguration {
    if (-not (Test-Path -LiteralPath $piSettingsPath -PathType Leaf)) {
        return
    }
    $settingsText = [System.IO.File]::ReadAllText($piSettingsPath)
    $settings = $settingsText | ConvertFrom-Json
    if ([string]$settings.externalEditor -eq $windowsCommand) {
        return
    }
    $backupPath = "$piSettingsPath.before-scratcheditor.bak"
    if (-not (Test-Path -LiteralPath $backupPath)) {
        Copy-Item -LiteralPath $piSettingsPath -Destination $backupPath
    }
    $propertyPattern = '(?m)^(?<prefix>[ \t]*"externalEditor"\s*:\s*)"(?:\\.|[^"\\])*"(?<suffix>\s*,?[ \t]*)$'
    $propertyMatches = [Regex]::Matches($settingsText, $propertyPattern)
    if ($propertyMatches.Count -gt 1) {
        throw "Expected at most one externalEditor property in $piSettingsPath; found $($propertyMatches.Count)."
    }
    if ($propertyMatches.Count -eq 1) {
        $serializedCommand = $windowsCommand | ConvertTo-Json -Compress
        $updatedText = [Regex]::Replace(
            $settingsText, $propertyPattern,
            [System.Text.RegularExpressions.MatchEvaluator]{
                param($match)
                $match.Groups['prefix'].Value + $serializedCommand + $match.Groups['suffix'].Value
            })
    } elseif ($null -eq $settings.PSObject.Properties["externalEditor"]) {
        $settings | Add-Member -NotePropertyName "externalEditor" -NotePropertyValue $windowsCommand
        $updatedText = ($settings | ConvertTo-Json -Depth 100) + [Environment]::NewLine
    } else {
        throw "Could not safely update the externalEditor property in $piSettingsPath."
    }
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($piSettingsPath, $updatedText, $utf8WithoutBom)
}

function Write-LocalInstallationDocument {
    $generatedAt = [DateTimeOffset]::Now.ToString("yyyy-MM-dd HH:mm:ss zzz")
    $documentLines = @(
        '# Local ScratchEditor installations',
        '',
        '> Generated by `scripts/configure-codex-editor.ps1 -Action Install`. This machine-specific file is ignored by Git.',
        ('> Generated at: `' + $generatedAt + '`'),
        '',
        '## Paths',
        '',
        '| Purpose | Local path |',
        '|---|---|',
        ('| Project root | `' + $projectRoot + '` |'),
        ('| Latest build source | `' + $resolvedSourceEditorPath + '` |'),
        ('| Stable Codex / pi editor | `' + $resolvedEditorPath + '` |'),
        ('| Stable AHK editor | `' + $resolvedAhkEditorPath + '` |'),
        ('| Regular-terminal `VISUAL` / `EDITOR` | `' + $windowsCommand + '` |'),
        '| VS Code integrated-terminal `VISUAL` / `EDITOR` | `code --wait` |',
        ('| Git Bash profile | `' + $bashProfilePath + '` |'),
        ('| VS Code user settings | `' + $vscodeSettingsPath + '` |'),
        ('| pi settings | `' + $piSettingsPath + '` |'),
        ('| Active AHK script | `' + $resolvedAhkScriptPath + '` |'),
        '',
        'Codex and pi share the same stable `CodexEditor` executable. Each `--wait` edit still runs as an independent file-mode process so concurrent sessions cannot overwrite one another.',
        'Ctrl+G uses VS Code inside the VS Code integrated terminal and the shared stable ScratchEditor copy in regular terminals.',
        'Scroll Lock and Win+F use the separate stable `AhkEditor` executable through the persistent `ScratchEditor.Stage1.v1` IPC instance.',
        'Clickable Codex file citations continue to use VS Code in every terminal through `file_opener = "vscode"`.',
        'Every successful `scripts/build.ps1` build automatically installs the just-built ScratchEditor into both stable directories and refreshes Codex, pi, AHK, Git Bash, and VS Code configuration.',
        'Pass `-SkipLocalInstall` only for an intentionally isolated build. Direct CMake builds do not synchronize the stable installations.',
        'The project `build/` and `.tools/` directories can be cleaned after installation, but they must be restored before the next update.',
        '',
        '## Update the stable copy',
        '',
        'Run these commands from the project root:',
        '',
        '```powershell',
        'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release',
        'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1 -Action Check',
        '```',
        '',
        '`build.ps1` deploys Qt, replaces both stable copies, safely restarts an existing stable AHK resident, and refreshes this document without changing either deployment path.'
    )
    $document = $documentLines -join [Environment]::NewLine
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $localInstallationDocument,
        $document.TrimStart() + [Environment]::NewLine,
        $utf8WithoutBom)
}

if ($Action -eq "Install") {
    [Environment]::SetEnvironmentVariable("VISUAL", $windowsCommand, "User")
    [Environment]::SetEnvironmentVariable("EDITOR", $windowsCommand, "User")
    Write-BashProfile
    Write-VsCodeTerminalEnvironment
    Write-PiEditorConfiguration
    Write-AhkEditorConfiguration
    Write-LocalInstallationDocument
}

$userVisual = [Environment]::GetEnvironmentVariable("VISUAL", "User")
$userEditor = [Environment]::GetEnvironmentVariable("EDITOR", "User")
$bashProfileConfigured = Test-BashProfile
$vscodeTerminalConfigured = Test-VsCodeTerminalEnvironment
$piDetected = Test-Path -LiteralPath $piSettingsPath -PathType Leaf
$piEditorConfigured = Test-PiEditorConfiguration
$ahkScriptDetected = Test-Path -LiteralPath $resolvedAhkScriptPath -PathType Leaf
$ahkEditorConfigured = Test-AhkEditorConfiguration
$installedCopiesMatch = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedEditorPath).Hash `
    -eq (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedAhkEditorPath).Hash
$persistentConfigurationValid = $userVisual -eq $windowsCommand `
    -and $userEditor -eq $windowsCommand `
    -and $bashProfileConfigured `
    -and $vscodeTerminalConfigured `
    -and $piEditorConfigured `
    -and $ahkEditorConfigured `
    -and $installedCopiesMatch

[PSCustomObject]@{
    ExpectedCommand = $windowsCommand
    StableCodexEditor = $resolvedEditorPath
    StableAhkEditor = $resolvedAhkEditorPath
    InstalledCopiesMatch = $installedCopiesMatch
    UserVisual = $userVisual
    UserEditor = $userEditor
    GitBashProfile = $bashProfilePath
    GitBashConfigured = $bashProfileConfigured
    VsCodeSettings = $vscodeSettingsPath
    VsCodeTerminalConfigured = $vscodeTerminalConfigured
    PiDetected = $piDetected
    PiSettings = $piSettingsPath
    PiEditorConfigured = $piEditorConfigured
    AhkScriptDetected = $ahkScriptDetected
    AhkScript = $resolvedAhkScriptPath
    AhkEditorConfigured = $ahkEditorConfigured
    LocalInstallationDocument = $localInstallationDocument
    CurrentProcessVisual = $env:VISUAL
    CurrentProcessEditor = $env:EDITOR
    RestartRequired = $env:VISUAL -ne $windowsCommand -or $env:EDITOR -ne $windowsCommand
    PersistentConfigurationValid = $persistentConfigurationValid
}

if (-not $persistentConfigurationValid) {
    throw "Persistent Codex external-editor configuration does not match the expected command."
}
