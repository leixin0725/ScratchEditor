[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,
    [string]$BuildDirectory = "build\release",
    [string]$OutputDirectory = "artifacts\release",
    [string]$QtLicenseFile
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
$editorExe = Join-Path $buildRoot "ScratchEditor.exe"
$deployQt = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64\bin\windeployqt.exe"
$qmlDirectory = Join-Path $projectRoot "qml"
$qtRoot = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64"
$mingwRoot = Join-Path $projectRoot ".tools\Qt\Tools\mingw1310_64"
$packageName = "ScratchEditor-$Version-windows-x64"
$archivePath = Join-Path $outputRoot "$packageName.zip"
$checksumPath = Join-Path $outputRoot "$packageName.zip.sha256"
$manifestArchivePath = Join-Path $outputRoot "ScratchEditor-$Version-winget-manifest.zip"
$stageRoot = $null

foreach ($requiredPath in @($editorExe, $deployQt, $qmlDirectory, $qtRoot, $mingwRoot)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required release packaging input is missing: $requiredPath"
    }
}

$projectVersion = [regex]::Match(
    (Get-Content -LiteralPath (Join-Path $projectRoot "CMakeLists.txt") -Raw),
    'project\(ScratchEditor\s+VERSION\s+(\d+\.\d+\.\d+)'
)
if (-not $projectVersion.Success -or $projectVersion.Groups[1].Value -ne $Version) {
    throw "Release version $Version does not match CMake project version $($projectVersion.Groups[1].Value)."
}

# Verify the vendored upstream snapshot before producing release assets.
$licenseSourcesPath = Join-Path $projectRoot "packaging\licenses\SOURCES.md"
foreach ($line in (Get-Content -LiteralPath $licenseSourcesPath)) {
    if ($line -match '^\| ([^|]+) \| https://[^|]+ \| ([a-f0-9]{64}) \|$') {
        $licensePath = Join-Path $projectRoot ("packaging\licenses\" + $Matches[1].Trim())
        $expectedHash = $Matches[2]
        if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $licensePath -Algorithm SHA256).Hash -ne $expectedHash) {
            throw "Vendored license is missing or differs from the upstream snapshot: $licensePath"
        }
    }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$stageRoot = Join-Path $temporaryRoot ("ScratchEditor-release-" + [guid]::NewGuid().ToString("N"))
$resolvedStageRoot = [IO.Path]::GetFullPath($stageRoot)
if (-not $resolvedStageRoot.StartsWith(
        $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create the staging directory outside the temporary directory: $stageRoot"
}

try {
    New-Item -ItemType Directory -Path $stageRoot | Out-Null
    Copy-Item -LiteralPath $editorExe -Destination (Join-Path $stageRoot "ScratchEditor.exe")

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
    & $deployQt @deployOptions (Join-Path $stageRoot "ScratchEditor.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "Qt deployment failed with exit code $LASTEXITCODE."
    }

    $configDirectory = Join-Path $stageRoot "config"
    New-Item -ItemType Directory -Path $configDirectory | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot "config\markdown-style.json") `
        -Destination (Join-Path $configDirectory "markdown-style.json")
    Copy-Item -LiteralPath (Join-Path $projectRoot "config\ui.json") `
        -Destination (Join-Path $configDirectory "ui.json")

    $licensesDirectory = Join-Path $stageRoot "licenses"
    New-Item -ItemType Directory -Path $licensesDirectory | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot "LICENSE") `
        -Destination (Join-Path $licensesDirectory "ScratchEditor-MIT.txt")
    Copy-Item -LiteralPath (Join-Path $projectRoot "third_party\lucide\LICENSE") `
        -Destination (Join-Path $licensesDirectory "Lucide-and-Feather-ISC-MIT.txt")
    $localQtLicenseFile = Join-Path $projectRoot "packaging\licenses\LGPL-3.0.txt"
    if ([string]::IsNullOrWhiteSpace($QtLicenseFile) -and
        (Test-Path -LiteralPath $localQtLicenseFile -PathType Leaf)) {
        $QtLicenseFile = $localQtLicenseFile
    }
    if ([string]::IsNullOrWhiteSpace($QtLicenseFile) -or
        -not (Test-Path -LiteralPath $QtLicenseFile -PathType Leaf)) {
        throw "Qt LGPL license text is required: $QtLicenseFile"
    }
    if ((Get-Content -LiteralPath $QtLicenseFile -Raw) -notmatch "GNU LESSER GENERAL PUBLIC LICENSE") {
        throw "Invalid Qt LGPL license text: $QtLicenseFile"
    }
    Copy-Item -LiteralPath $QtLicenseFile -Destination (Join-Path $licensesDirectory "LGPL-3.0.txt")
    Copy-Item -LiteralPath (Join-Path $projectRoot "packaging\licenses\GPL-3.0.txt") -Destination $licensesDirectory
    foreach ($module in @("qtbase", "qtdeclarative", "qtsvg")) {
        $moduleLicenses = Join-Path $projectRoot "packaging\licenses\$module"
        if (-not (Test-Path -LiteralPath $moduleLicenses -PathType Container)) {
            throw "Qt module license texts are missing: $moduleLicenses"
        }
        Copy-Item -LiteralPath $moduleLicenses -Destination $licensesDirectory -Recurse
    }

    $runtimeLicenseFiles = @(
        @{ Source = "licenses\gcc\COPYING.RUNTIME"; Name = "GCC-Runtime-Exception.txt" },
        @{ Source = "licenses\gcc\COPYING3.LIB"; Name = "GCC-COPYING3.LIB.txt" },
        @{ Source = "licenses\winpthreads\COPYING"; Name = "winpthreads-COPYING.txt" },
        @{ Source = "licenses\mingw-w64\COPYING.MinGW-w64-runtime.txt"; Name = "MinGW-w64-runtime.txt" }
    )
    foreach ($license in $runtimeLicenseFiles) {
        $sourcePath = Join-Path $mingwRoot $license.Source
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Required runtime license file is missing: $sourcePath"
        }
        Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $licensesDirectory $license.Name)
    }

    $qtSbomDirectory = Join-Path $licensesDirectory "qt-sbom"
    New-Item -ItemType Directory -Path $qtSbomDirectory | Out-Null
    $qtComponentNotices = [Collections.Generic.List[string]]::new()
    foreach ($module in @("qtbase", "qtdeclarative", "qtsvg")) {
        $sbomPath = Join-Path $qtRoot "sbom\$module-6.10.2.spdx.json"
        if (-not (Test-Path -LiteralPath $sbomPath -PathType Leaf)) {
            throw "Qt SBOM is missing for ${module}: $sbomPath"
        }
        Copy-Item -LiteralPath $sbomPath -Destination $qtSbomDirectory
        $sbom = Get-Content -LiteralPath $sbomPath -Raw | ConvertFrom-Json
        $qtComponentNotices.Add("Qt module: $module (SBOM also describes build tools not distributed in this ZIP)")
        foreach ($component in $sbom.packages) {
            $qtComponentNotices.Add(("{0}`nCopyright: {1}`nLicense: {2}`nSource: {3}`n" -f
                $component.name, $component.copyrightText, $component.licenseDeclared, $component.downloadLocation))
        }
        foreach ($license in $sbom.hasExtractedLicensingInfos) {
            $qtComponentNotices.Add($license.licenseId + "`n" + $license.extractedText)
        }
    }

    [IO.File]::WriteAllText(
        (Join-Path $licensesDirectory "Qt-component-notices.txt"),
        ($qtComponentNotices -join "`r`n"), [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $projectRoot "packaging\licenses\SOURCES.md") -Destination $licensesDirectory

    $notices = @'
ScratchEditor third-party notices
==================================

ScratchEditor
-------------
Copyright (c) 2026 Lex. The application source is distributed under the MIT License.
See licenses/ScratchEditor-MIT.txt.

Qt 6.10.2
---------
This distribution includes dynamically linked Qt libraries and plugins from Qt 6.10.2.
Qt modules and their third-party components are available under the applicable Qt open-source
licenses. The Qt SBOM files in licenses/qt-sbom describe the components and license identifiers
for the shipped Qt modules. Readable component notices: licenses/Qt-component-notices.txt.
Qt license terms: https://doc.qt.io/qt-6/licensing.html
LGPL version 3 text: licenses/LGPL-3.0.txt; incorporated GPLv3 terms: licenses/GPL-3.0.txt.
Module license texts are in licenses/qtbase, licenses/qtdeclarative and licenses/qtsvg.
Canonical LGPL text:
https://www.gnu.org/licenses/lgpl-3.0.txt
Users may replace the dynamically linked Qt libraries with compatible modified versions.
Reverse engineering for debugging such modifications is permitted under the LGPL.
Qt 6.10.2 source packages: https://download.qt.io/official_releases/qt/6.10/6.10.2/

MinGW runtime
-------------
The distribution includes GCC, MinGW-w64, and winpthreads runtime libraries. Relevant license
and exception texts are included in licenses/. The Qt MinGW toolchain and source information are
available from https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_mingw/.

Lucide and Feather icons
------------------------
Icon notices and license terms are included in licenses/Lucide-and-Feather-ISC-MIT.txt.
'@
    [IO.File]::WriteAllText(
        (Join-Path $stageRoot "THIRD-PARTY-NOTICES.txt"),
        $notices.Replace("`n", "`r`n"),
        [Text.UTF8Encoding]::new($false)
    )

    $packageReadme = @'
ScratchEditor portable package

Extract this folder and run ScratchEditor.exe. No Qt developer tools are required.
The package does not configure Codex, pi, AutoHotkey, WSL, Git Bash, VS Code, or user environment variables.

User settings, UI templates, and clipboard history are stored in the current Windows user's
ScratchEditor application configuration directory, outside this extracted folder. Removing this
folder does not remove those user files.

See THIRD-PARTY-NOTICES.txt for bundled component notices and licenses.
'@
    [IO.File]::WriteAllText(
        (Join-Path $stageRoot "README.txt"),
        $packageReadme.Replace("`n", "`r`n"),
        [Text.UTF8Encoding]::new($false)
    )

    if (Test-Path -LiteralPath $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $stageRoot,
        $archivePath,
        [IO.Compression.CompressionLevel]::Optimal,
        $false
    )

    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(
        $checksumPath,
        "$archiveHash *$([IO.Path]::GetFileName($archivePath))`r`n",
        [Text.UTF8Encoding]::new($false)
    )

    $manifestDirectory = Join-Path $stageRoot "_winget-manifest"
    New-Item -ItemType Directory -Path $manifestDirectory | Out-Null
    $installerUrl = "https://github.com/leixin0725/ScratchEditor/releases/download/v$Version/$([IO.Path]::GetFileName($archivePath))"
    $schemaVersion = "1.12.0"
    $identifier = "leixin0725.ScratchEditor"
    $common = @(
        "# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.$schemaVersion.schema.json",
        "PackageIdentifier: $identifier",
        "PackageVersion: $Version"
    )
    $versionManifest = @(
        $common
        "DefaultLocale: en-US"
        "ManifestType: version"
        "ManifestVersion: $schemaVersion"
    ) -join "`r`n"
    $localeManifest = @(
        "# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.$schemaVersion.schema.json",
        "PackageIdentifier: $identifier",
        "PackageVersion: $Version",
        "PackageLocale: en-US",
        "Publisher: leixin0725",
        "PackageName: ScratchEditor",
        "License: MIT",
        "ShortDescription: Lightweight Windows scratch text editor",
        "PackageUrl: https://github.com/leixin0725/ScratchEditor",
        "ManifestType: defaultLocale",
        "ManifestVersion: $schemaVersion"
    ) -join "`r`n"
    $installerManifest = @(
        "# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.$schemaVersion.schema.json",
        "PackageIdentifier: $identifier",
        "PackageVersion: $Version",
        "MinimumOSVersion: 10.0.22000.0",
        "Installers:",
        "  - Architecture: x64",
        "    InstallerType: zip",
        "    InstallerUrl: $installerUrl",
        "    InstallerSha256: $archiveHash",
        "    NestedInstallerType: portable",
        "    NestedInstallerFiles:",
        "      - RelativeFilePath: ScratchEditor.exe",
        "        PortableCommandAlias: ScratchEditor",
        "ManifestType: installer",
        "ManifestVersion: $schemaVersion"
    ) -join "`r`n"
    foreach ($entry in @(
        @{ Name = "$identifier.yaml"; Content = $versionManifest },
        @{ Name = "$identifier.locale.en-US.yaml"; Content = $localeManifest },
        @{ Name = "$identifier.installer.yaml"; Content = $installerManifest }
    )) {
        [IO.File]::WriteAllText(
            (Join-Path $manifestDirectory $entry.Name),
            $entry.Content + "`r`n",
            [Text.UTF8Encoding]::new($false)
        )
    }
    if (Test-Path -LiteralPath $manifestArchivePath) {
        Remove-Item -LiteralPath $manifestArchivePath -Force
    }
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $manifestDirectory,
        $manifestArchivePath,
        [IO.Compression.CompressionLevel]::Optimal,
        $false
    )

    [pscustomobject]@{
        Version = $Version
        Package = $archivePath
        Sha256 = $archiveHash
        ChecksumFile = $checksumPath
        WinGetManifest = $manifestArchivePath
    }
}
finally {
    if ($stageRoot -and (Test-Path -LiteralPath $stageRoot)) {
        $resolvedStageRoot = [IO.Path]::GetFullPath($stageRoot)
        if ($resolvedStageRoot.StartsWith(
                $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolvedStageRoot).StartsWith("ScratchEditor-release-")) {
            Remove-Item -LiteralPath $resolvedStageRoot -Recurse -Force
        }
    }
}
