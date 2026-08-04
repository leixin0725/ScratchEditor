[CmdletBinding()]
param(
    [ValidateSet("release", "editing", "window-ui")]
    [string]$Preset = "release",
    [switch]$SkipDeployment,
    [switch]$SkipLocalInstall
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = Join-Path $projectRoot ".tools\Qt\Tools\CMake_64\bin\cmake.exe"
$deployQt = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64\bin\windeployqt.exe"
$buildDir = Join-Path $projectRoot "build\$Preset"

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake toolchain is missing. Install the workspace-local Qt toolchain first."
}

Push-Location $projectRoot
try {
    & $cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }
    & $cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE"
    }

    if (-not $SkipDeployment) {
        $deployOptions = @(
            "--release",
            "--no-translations",
            "--no-system-d3d-compiler",
            "--no-system-dxc-compiler",
            "--no-opengl-sw",
            "--compiler-runtime",
            "--skip-plugin-types", "qmltooling,generic",
            "--qmldir", (Join-Path $projectRoot "qml")
        )
        & $deployQt @deployOptions (Join-Path $buildDir "ScratchEditor.exe")
        if ($LASTEXITCODE -ne 0) {
            throw "Qt deployment failed for ScratchEditor with exit code $LASTEXITCODE"
        }
        & $deployQt --release --no-translations --compiler-runtime `
            (Join-Path $buildDir "ScratchEditorPerf.exe")
        if ($LASTEXITCODE -ne 0) {
            throw "Qt deployment failed for ScratchEditorPerf with exit code $LASTEXITCODE"
        }
    }

    if (-not $SkipLocalInstall) {
        $installScript = Join-Path $PSScriptRoot "configure-codex-editor.ps1"
        & $installScript -Action Install `
            -SourceEditorPath (Join-Path $buildDir "ScratchEditor.exe")
        if ($LASTEXITCODE -ne 0) {
            throw "Stable ScratchEditor installation failed with exit code $LASTEXITCODE"
        }
    }
}
finally {
    Pop-Location
}
