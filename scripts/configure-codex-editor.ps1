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
$startMarker = "# >>> ScratchEditor Codex external editor >>>"
$endMarker = "# <<< ScratchEditor Codex external editor <<<"
$managedBlockLines = @(
    $startMarker,
    "# Managed by $($PSScriptRoot.Replace('\', '/'))/configure-codex-editor.ps1",
    "export VISUAL='$bashCommand'",
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

if ($Action -eq "Install") {
    [Environment]::SetEnvironmentVariable("VISUAL", $windowsCommand, "User")
    [Environment]::SetEnvironmentVariable("EDITOR", $windowsCommand, "User")
    Write-BashProfile
}

$userVisual = [Environment]::GetEnvironmentVariable("VISUAL", "User")
$userEditor = [Environment]::GetEnvironmentVariable("EDITOR", "User")
$bashProfileConfigured = Test-BashProfile
$persistentConfigurationValid = $userVisual -eq $windowsCommand `
    -and $userEditor -eq $windowsCommand `
    -and $bashProfileConfigured

[PSCustomObject]@{
    ExpectedCommand = $windowsCommand
    UserVisual = $userVisual
    UserEditor = $userEditor
    GitBashProfile = $bashProfilePath
    GitBashConfigured = $bashProfileConfigured
    CurrentProcessVisual = $env:VISUAL
    CurrentProcessEditor = $env:EDITOR
    RestartRequired = $env:VISUAL -ne $windowsCommand -or $env:EDITOR -ne $windowsCommand
    PersistentConfigurationValid = $persistentConfigurationValid
}

if (-not $persistentConfigurationValid) {
    throw "Persistent Codex external-editor configuration does not match the expected command."
}
