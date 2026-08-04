[CmdletBinding()]
param(
    [ValidateSet("Install", "Check")]
    [string]$Action = "Install",
    [string]$SourceEditorPath,
    [string]$InstallDirectory
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($InstallDirectory)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw "LOCALAPPDATA is not available."
    }
    $InstallDirectory = Join-Path $env:LOCALAPPDATA "ScratchEditor\CodexEditor"
}

$resolvedInstallDirectory = [System.IO.Path]::GetFullPath($InstallDirectory)
$resolvedEditorPath = Join-Path $resolvedInstallDirectory "ScratchEditor.exe"

if ($Action -eq "Install") {
    if ([string]::IsNullOrWhiteSpace($SourceEditorPath)) {
        $SourceEditorPath = Join-Path $projectRoot "build\release\ScratchEditor.exe"
    }
    $resolvedSourceEditorPath = [System.IO.Path]::GetFullPath($SourceEditorPath)
    $deployQt = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64\bin\windeployqt.exe"
    $qmlDirectory = Join-Path $projectRoot "qml"
    $markdownStyle = Join-Path $projectRoot "config\markdown-style.json"
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

    New-Item -ItemType Directory -Path $resolvedInstallDirectory -Force | Out-Null
    Copy-Item -LiteralPath $resolvedSourceEditorPath -Destination $resolvedEditorPath -Force
    $installedConfigDirectory = Join-Path $resolvedInstallDirectory "config"
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
    & $deployQt @deployOptions $resolvedEditorPath
    if ($LASTEXITCODE -ne 0) {
        throw "Qt deployment failed with exit code $LASTEXITCODE."
    }
}

$editorDirectory = Split-Path -Parent $resolvedEditorPath
$requiredRuntimePaths = @(
    $resolvedEditorPath,
    (Join-Path $editorDirectory "Qt6Core.dll"),
    (Join-Path $editorDirectory "Qt6Gui.dll"),
    (Join-Path $editorDirectory "Qt6Qml.dll"),
    (Join-Path $editorDirectory "Qt6Quick.dll"),
    (Join-Path $editorDirectory "platforms\qwindows.dll")
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
$localInstallationDocument = Join-Path $projectRoot "docs\codex-editor-installation.local.md"
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

function Write-LocalInstallationDocument {
    $generatedAt = [DateTimeOffset]::Now.ToString("yyyy-MM-dd HH:mm:ss zzz")
    $ahkScriptPath = "D:\Documents\AutoHotkey\KeysRedirect.ahk"
    $ahkEditorPath = Join-Path $projectRoot "build\stage4\ScratchEditor.exe"
    $documentLines = @(
        '# Local Codex editor installation',
        '',
        '> Generated by `scripts/configure-codex-editor.ps1 -Action Install`. This machine-specific file is ignored by Git.',
        ('> Generated at: `' + $generatedAt + '`'),
        '',
        '## Paths',
        '',
        '| Purpose | Local path |',
        '|---|---|',
        ('| Project root | `' + $projectRoot + '` |'),
        ('| Build source | `' + $resolvedSourceEditorPath + '` |'),
        ('| Stable Codex editor | `' + $resolvedEditorPath + '` |'),
        ('| Regular-terminal `VISUAL` / `EDITOR` | `' + $windowsCommand + '` |'),
        '| VS Code integrated-terminal `VISUAL` / `EDITOR` | `code --wait` |',
        ('| Git Bash profile | `' + $bashProfilePath + '` |'),
        ('| VS Code user settings | `' + $vscodeSettingsPath + '` |'),
        ('| Active AHK script | `' + $ahkScriptPath + '` |'),
        ('| AHK default ScratchEditor target | `' + $ahkEditorPath + '` |'),
        '',
        'Ctrl+G uses VS Code inside the VS Code integrated terminal and the stable ScratchEditor copy in regular terminals.',
        'Clickable Codex file citations continue to use VS Code in every terminal through `file_opener = "vscode"`.',
        'Rebuilding the project does not update the stable ScratchEditor copy automatically.',
        'The project `build/` and `.tools/` directories can be cleaned after installation, but they must be restored before the next update.',
        '',
        '## Update the stable copy',
        '',
        'Run these commands from the project root:',
        '',
        '```powershell',
        'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release',
        'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1 -Action Install',
        'powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure-codex-editor.ps1 -Action Check',
        '```',
        '',
        '`Install` replaces the stable copy and refreshes this document without changing the deployment path.'
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
    Write-LocalInstallationDocument
}

$userVisual = [Environment]::GetEnvironmentVariable("VISUAL", "User")
$userEditor = [Environment]::GetEnvironmentVariable("EDITOR", "User")
$bashProfileConfigured = Test-BashProfile
$vscodeTerminalConfigured = Test-VsCodeTerminalEnvironment
$persistentConfigurationValid = $userVisual -eq $windowsCommand `
    -and $userEditor -eq $windowsCommand `
    -and $bashProfileConfigured `
    -and $vscodeTerminalConfigured

[PSCustomObject]@{
    ExpectedCommand = $windowsCommand
    UserVisual = $userVisual
    UserEditor = $userEditor
    GitBashProfile = $bashProfilePath
    GitBashConfigured = $bashProfileConfigured
    VsCodeSettings = $vscodeSettingsPath
    VsCodeTerminalConfigured = $vscodeTerminalConfigured
    LocalInstallationDocument = $localInstallationDocument
    CurrentProcessVisual = $env:VISUAL
    CurrentProcessEditor = $env:EDITOR
    RestartRequired = $env:VISUAL -ne $windowsCommand -or $env:EDITOR -ne $windowsCommand
    PersistentConfigurationValid = $persistentConfigurationValid
}

if (-not $persistentConfigurationValid) {
    throw "Persistent Codex external-editor configuration does not match the expected command."
}
