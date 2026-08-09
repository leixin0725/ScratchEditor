[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $projectRoot ".tools"
$targetRoot = Join-Path $toolsRoot "Qt"
$requiredFiles = @(
    "6.10.2\mingw_64\bin\qmake.exe",
    "6.10.2\mingw_64\bin\windeployqt.exe",
    "Tools\mingw1310_64\bin\g++.exe",
    "Tools\CMake_64\bin\cmake.exe",
    "Tools\Ninja\ninja.exe"
)

function Test-Toolchain {
    param([Parameter(Mandatory)][string]$Root)

    foreach ($relativePath in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $relativePath) -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

if (Test-Toolchain -Root $targetRoot) {
    Write-Host "Workspace toolchain is already complete: $targetRoot"
    exit 0
}

New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
$targetItem = Get-Item -LiteralPath $targetRoot -Force -ErrorAction SilentlyContinue
if ($targetItem -and $targetItem.LinkType) {
    throw "Refusing to replace linked toolchain path: $targetRoot -> $($targetItem.Target -join ', ')"
}

$targetHasContent = $targetItem -and (Get-ChildItem -LiteralPath $targetRoot -Force | Select-Object -First 1)
if ($targetHasContent -and -not $Force) {
    throw "Incomplete toolchain directory is not empty. Re-run with -Force after reviewing: $targetRoot"
}

$stagingRoot = Join-Path $toolsRoot ("Qt.restore-" + [Guid]::NewGuid().ToString("N"))
$bootstrapRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ScratchEditor-aqt-" + [Guid]::NewGuid().ToString("N"))
$bootstrapPython = Join-Path $bootstrapRoot "Scripts\python.exe"

try {
    Write-Host "Creating temporary aqtinstall environment..."
    & py -3.13 -m venv $bootstrapRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create the Python 3.13 bootstrap environment."
    }

    & $bootstrapPython -m pip install --disable-pip-version-check --quiet "aqtinstall==3.3.0"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install aqtinstall 3.3.0."
    }

    Write-Host "Installing Qt 6.10.2 for MinGW..."
    & $bootstrapPython -m aqt install-qt windows desktop 6.10.2 win64_mingw --outputdir $stagingRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Qt 6.10.2 installation failed."
    }

    Write-Host "Installing MinGW 13.1, CMake, and Ninja..."
    & $bootstrapPython -m aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 --outputdir $stagingRoot
    if ($LASTEXITCODE -ne 0) {
        throw "MinGW 13.1 installation failed."
    }
    & $bootstrapPython -m aqt install-tool windows desktop tools_cmake qt.tools.cmake --outputdir $stagingRoot
    if ($LASTEXITCODE -ne 0) {
        throw "CMake installation failed."
    }
    & $bootstrapPython -m aqt install-tool windows desktop tools_ninja qt.tools.ninja --outputdir $stagingRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Ninja installation failed."
    }

    if (-not (Test-Toolchain -Root $stagingRoot)) {
        throw "Downloaded toolchain does not match the layout required by CMakePresets.json."
    }

    if ($targetItem) {
        Remove-Item -LiteralPath $targetRoot -Recurse -Force
    }
    Move-Item -LiteralPath $stagingRoot -Destination $targetRoot

    Write-Host "Workspace toolchain restored successfully: $targetRoot"
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $bootstrapRoot) {
        Remove-Item -LiteralPath $bootstrapRoot -Recurse -Force
    }
}
