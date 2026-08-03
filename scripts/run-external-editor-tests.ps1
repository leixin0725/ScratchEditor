[CmdletBinding()]
param(
    [string]$BuildDir = "build/release"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = Join-Path $projectRoot $BuildDir
$unitTests = Join-Path $resolvedBuildDir "ScratchEditorExternalFileTests.exe"
$processTests = Join-Path $resolvedBuildDir "ScratchEditorExternalProcessTests.exe"
$editor = Join-Path $resolvedBuildDir "ScratchEditor.exe"

foreach ($requiredPath in @($unitTests, $processTests, $editor)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required external-editor test dependency is missing: $requiredPath"
    }
}

& $unitTests
if ($LASTEXITCODE -ne 0) {
    throw "External file session tests failed with exit code $LASTEXITCODE"
}

& $processTests $editor
if ($LASTEXITCODE -ne 0) {
    throw "External editor process tests failed with exit code $LASTEXITCODE"
}

Write-Output "All external editor tests passed."
