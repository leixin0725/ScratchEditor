[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackagePath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$projectRoot = Split-Path -Parent $PSScriptRoot
$packagePath = [IO.Path]::GetFullPath((Join-Path $projectRoot $PackagePath))
foreach ($requiredPath in @($packagePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required package smoke-test input is missing: $requiredPath"
    }
}

$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$extractRoot = Join-Path $temporaryRoot ("ScratchEditor release smoke-" + [guid]::NewGuid().ToString("N"))
$resolvedExtractRoot = [IO.Path]::GetFullPath($extractRoot)
if (-not $resolvedExtractRoot.StartsWith(
        $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create an extraction directory outside the temporary directory: $extractRoot"
}

$previousPath = $env:PATH
$savedEnvironment = @{}
$testEnvironmentNames = @(
    "SCRATCHEDITOR_SERVER_NAME",
    "SCRATCHEDITOR_SETTINGS_FILE",
    "SCRATCHEDITOR_EXTERNAL_TEST_STATUS_FILE",
    "SCRATCHEDITOR_EXTERNAL_TEST_TEXT",
    "SCRATCHEDITOR_EXTERNAL_TEST_DISCARD"
)
foreach ($name in $testEnvironmentNames) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

function Invoke-PackagedSession {
    param(
        [Parameter(Mandatory)] [string]$Mode,
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string]$InitialText,
        [string]$ReplacementText
    )

    [IO.File]::WriteAllText($FilePath, $InitialText, [Text.UTF8Encoding]::new($false))
    $settingsFile = Join-Path $script:smokeDataDirectory "$Mode-settings.ini"
    $statusFile = Join-Path $script:smokeDataDirectory "$Mode-status.json"
    $env:SCRATCHEDITOR_SERVER_NAME = "ScratchEditor.ReleasePackage.$Mode.$([guid]::NewGuid().ToString('N'))"
    $env:SCRATCHEDITOR_SETTINGS_FILE = $settingsFile
    $env:SCRATCHEDITOR_EXTERNAL_TEST_STATUS_FILE = $statusFile
    Remove-Item Env:SCRATCHEDITOR_EXTERNAL_TEST_TEXT -ErrorAction SilentlyContinue
    Remove-Item Env:SCRATCHEDITOR_EXTERNAL_TEST_DISCARD -ErrorAction SilentlyContinue
    if ($Mode -eq "save") {
        $env:SCRATCHEDITOR_EXTERNAL_TEST_TEXT = $ReplacementText
    }
    else {
        $env:SCRATCHEDITOR_EXTERNAL_TEST_DISCARD = "1"
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $script:stagedEditor
    $startInfo.Arguments = "--test-mode --wait `"$FilePath`""
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        $process.WaitForExit(5000) | Out-Null
        throw "Packaged $Mode session failed to exit within 15 seconds."
    }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    if ($process.ExitCode -ne 0) {
        throw "Packaged $Mode session exited with $($process.ExitCode). stdout=$stdout stderr=$stderr"
    }
    if (-not (Test-Path -LiteralPath $settingsFile -PathType Leaf)) {
        throw "Packaged $Mode session did not use the isolated settings file."
    }
    if (-not (Test-Path -LiteralPath $statusFile -PathType Leaf)) {
        throw "Packaged $Mode session did not report its isolated startup status."
    }
    $status = Get-Content -LiteralPath $statusFile -Raw | ConvertFrom-Json
    if ($status.historyAvailable -or $status.nativeClipboardAccessAttempts -ne 0) {
        throw "Packaged external mode accessed clipboard history or native clipboard: $($status | ConvertTo-Json -Compress)"
    }
}

try {
    New-Item -ItemType Directory -Path $extractRoot | Out-Null
    Expand-Archive -LiteralPath $packagePath -DestinationPath $extractRoot
    $stagedEditor = Join-Path $extractRoot "ScratchEditor.exe"
    $smokeDataDirectory = Join-Path $extractRoot "_smoke-data"
    New-Item -ItemType Directory -Path $smokeDataDirectory | Out-Null
    foreach ($required in @(
        $stagedEditor,
        (Join-Path $extractRoot "config\markdown-style.json"),
        (Join-Path $extractRoot "config\ui.json"),
        (Join-Path $extractRoot "THIRD-PARTY-NOTICES.txt"),
        (Join-Path $extractRoot "licenses\LGPL-3.0.txt"),
        (Join-Path $extractRoot "licenses\GPL-3.0.txt"),
        (Join-Path $extractRoot "licenses\Qt-component-notices.txt")
    )) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Release package is missing required file: $required"
        }
    }

    $forbiddenFiles = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File | Where-Object {
        $_.Name -match '^(ScratchEditor.*Tests|ScratchEditorPerf)\.exe$' -or
        $_.Name -in @('CMakeCache.txt', 'build.ninja', 'compile_commands.json')
    })
    if ($forbiddenFiles.Count -gt 0) {
        throw "Release package contains build/test files: $($forbiddenFiles.FullName -join ', ')"
    }

    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $saveFile = Join-Path $smokeDataDirectory "prompt with spaces.md"
    $unicodeSample = [string]::Concat(
        "Unicode: ", [char]0x4F60, [char]0x597D, " ", [char]::ConvertFromUtf32(0x1F9EA), "`n"
    )
    $replacementText = "# packaged edit`n$unicodeSample"
    Invoke-PackagedSession -Mode "save" -FilePath $saveFile `
        -InitialText "# original`n" -ReplacementText $replacementText
    $savedText = [IO.File]::ReadAllText($saveFile, [Text.UTF8Encoding]::new($false))
    if ($savedText -ne $replacementText) {
        throw "Packaged save session wrote unexpected UTF-8 content: $savedText"
    }

    $discardFile = Join-Path $smokeDataDirectory "discard with spaces.md"
    $originalDiscardText = "# preserve original`n"
    Invoke-PackagedSession -Mode "discard" -FilePath $discardFile `
        -InitialText $originalDiscardText
    $discardedText = [IO.File]::ReadAllText($discardFile, [Text.UTF8Encoding]::new($false))
    if ($discardedText -ne $originalDiscardText) {
        throw "Packaged discard session changed the source file."
    }
    Write-Output "Release package smoke test passed: $packagePath"
}
finally {
    $env:PATH = $previousPath
    foreach ($name in $testEnvironmentNames) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
    }
    if (Test-Path -LiteralPath $extractRoot) {
        $resolvedExtractRoot = [IO.Path]::GetFullPath($extractRoot)
        if ($resolvedExtractRoot.StartsWith(
                $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolvedExtractRoot).StartsWith("ScratchEditor release smoke-")) {
            Remove-Item -LiteralPath $resolvedExtractRoot -Recurse -Force
        }
    }
}
