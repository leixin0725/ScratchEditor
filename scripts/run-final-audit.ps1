[CmdletBinding()]
param(
    [string]$OriginalAhkPath = "",
    [string]$OutputPath = "artifacts\baselines\final-audit.json",
    [switch]$RequireGit,
    [switch]$RequireClean,
    [switch]$NoWrite
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = $env:SCRATCHEDITOR_ORIGINAL_AHK
}
if ([string]::IsNullOrWhiteSpace($OriginalAhkPath)) {
    $OriginalAhkPath = Join-Path $projectRoot "integration\KeysRedirect.QtMigration.ahk"
}

$requiredFiles = @(
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "docs\archive\ScratchEditor-Migration.md",
    ".gitattributes",
    ".gitignore",
    "src\appsettings.cpp",
    "src\clipboardgateway.h",
    "src\clipboardgateway.cpp",
    "src\clipboardhistorycommandgate.h",
    "src\clipboardhistorymodel.h",
    "src\clipboardhistorymodel.cpp",
    "src\clipboardhistorystore.h",
    "src\clipboardhistorystore.cpp",
    "src\editorcontroller.cpp",
    "src\editorcommandregistry.cpp",
    "src\markdownhighlighter.cpp",
    "qml\Main.qml",
    "tests\perf_main.cpp",
    "tests\system_main.cpp",
    "tests\editing_main.cpp",
    "tests\clipboardhistory_main.cpp",
    "tests\externaleditorprocess_main.cpp",
    "tests\window_ui_main.cpp",
    "tests\fixtures\KeysRedirect.IpcTest.ahk",
    "tests\README.md",
    "integration\KeysRedirect.QtMigration.ahk",
    "integration\README.md",
    "scripts\run-clipboard-history-tests.ps1",
    "docs\README.md",
    "docs\archive\STAGE1-REPORT.md",
    "docs\archive\STAGE2-REPORT.md",
    "docs\archive\STAGE3-REPORT.md",
    "docs\archive\STAGE4-REPORT.md",
    "docs\archive\STAGE5-REPORT.md"
)

$requiredBaselines = @(
    "stage1-results-20260801-232528.json",
    "stage1-ahk-ipc-20260801-232714.json",
    "stage1-results-20260801-235518.json",
    "stage2-results-20260801-235527.json",
    "stage2-ahk-copy-20260801-235527.diff",
    "stage3-results-20260802-121348.json",
    "stage3-performance-20260802-121211.json",
    "stage4-results-20260802-130258.json",
    "stage4-performance-20260802-130336.json",
    "stage5-functional-20260802-131459.json",
    "stage5-ahk-ipc-20260802-131519.json",
    "stage5-performance-20260802-131542.json"
)

$missingFiles = @($requiredFiles | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $projectRoot $_))
})
$baselineDir = Join-Path $projectRoot "artifacts\baselines"
$missingBaselines = @($requiredBaselines | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $baselineDir $_))
})

$baselineFailures = @()
Get-ChildItem -LiteralPath $baselineDir -Filter "*.json" -File | ForEach-Object {
    if ($_.Name -eq "final-audit.json") {
        return
    }
    try {
        $document = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
        $passed = if ($null -ne $document.allPassed) {
            [bool]$document.allPassed
        }
        elseif ($null -ne $document.passed) {
            [bool]$document.passed
        }
        else {
            $true
        }
        if (-not $passed) {
            $baselineFailures += $_.Name
        }
    }
    catch {
        $baselineFailures += $_.Name
    }
}

$sourceFiles = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot "src"), `
    (Join-Path $projectRoot "qml") -File -Recurse)
$browserReferences = @($sourceFiles | Select-String -Pattern "WebEngine|WebView|Chromium" `
    -CaseSensitive:$false)
$deferredCommandReferences = @(
    Get-Content -LiteralPath (Join-Path $projectRoot "src\editorcommandregistry.cpp") `
        -Encoding UTF8 |
        Select-String -Pattern "togglePreview|historyDrafts|pinnedDraft|multiCursor|plugins|lsp" `
            -CaseSensitive:$false
)
$settingsReferencesOutsideStore = @(
    Get-ChildItem -LiteralPath (Join-Path $projectRoot "src") -File |
        Where-Object { $_.Name -notlike "appsettings.*" } |
        Select-String -Pattern "QSettings" -CaseSensitive
)

$clipboardApiPattern = "AddClipboardFormatListener|RemoveClipboardFormatListener|" +
    "GetClipboardSequenceNumber|OpenClipboard|CloseClipboard|GetClipboardData|SetClipboardData"
$clipboardApiReferencesOutsideGateway = @(
    Get-ChildItem -LiteralPath (Join-Path $projectRoot "src") -File |
        Where-Object { $_.Name -ne "clipboardgateway.cpp" } |
        Select-String -Pattern $clipboardApiPattern -CaseSensitive
)
$gatewaySource = Get-Content -LiteralPath `
    (Join-Path $projectRoot "src\clipboardgateway.cpp") -Raw -Encoding UTF8
$missingGatewayClipboardApis = @(
    @("AddClipboardFormatListener", "RemoveClipboardFormatListener",
      "GetClipboardSequenceNumber", "OpenClipboard", "CloseClipboard",
      "GetClipboardData", "SetClipboardData") |
        Where-Object { -not $gatewaySource.Contains($_) }
)

$migrationCopy = Get-Content -LiteralPath `
    (Join-Path $projectRoot "integration\KeysRedirect.QtMigration.ahk") -Raw -Encoding UTF8
$migrationBoundary = (
    $migrationCopy.Contains("UseQtScratchEditor") -and
    $migrationCopy.Contains("EnsureQtScratchEditorAndSend") -and
    $migrationCopy.Contains("ToggleScratch(")
)

$rootAllowedFiles = @(
    ".gitattributes", ".gitignore", "CMakeLists.txt", "CMakePresets.json", "README.md"
)
$unexpectedRootFiles = @(
    Get-ChildItem -LiteralPath $projectRoot -File -Force |
        Where-Object { $rootAllowedFiles -notcontains $_.Name } |
        ForEach-Object { $_.Name }
)
$looseArtifacts = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot "artifacts") -File)

$scriptParseFailures = @()
Get-ChildItem -LiteralPath (Join-Path $projectRoot "scripts") -Filter "*.ps1" -File |
    ForEach-Object {
        try {
            [void][scriptblock]::Create((Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8))
        }
        catch {
            $scriptParseFailures += $_.Name
        }
    }

$expectedAhkHash = "8BB8FFEFEBD9A6C90C102F66583D517C6C5CF83D36200A3D4E77D413C77B41C9"
$actualAhkHash = if (Test-Path -LiteralPath $OriginalAhkPath) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $OriginalAhkPath).Hash
}
else {
    ""
}

$gitInitialized = $false
$gitStatus = ""
$trackedExcluded = @()
$gitWhitespaceClean = $true
$unstagedClean = $true
$repositoryClean = $true
Push-Location $projectRoot
try {
    $gitMarker = Join-Path $projectRoot ".git"
    if (Test-Path -LiteralPath $gitMarker) {
        & git rev-parse --is-inside-work-tree *> $null
        $gitInitialized = ($LASTEXITCODE -eq 0)
    }
    if ($gitInitialized) {
        $tracked = @(& git ls-files)
        $trackedExcluded = @($tracked | Where-Object {
            $_ -match '^(\.tools/|build/|artifacts/(?!baselines/))'
        })
        & git diff --check *> $null
        $unstagedWhitespace = $LASTEXITCODE
        & git diff --cached --check *> $null
        $stagedWhitespace = $LASTEXITCODE
        $gitWhitespaceClean = ($unstagedWhitespace -eq 0 -and $stagedWhitespace -eq 0)
        & git diff --quiet
        $unstagedClean = ($LASTEXITCODE -eq 0)
        $gitStatus = (& git status --short) -join "`n"
        $repositoryClean = [string]::IsNullOrWhiteSpace($gitStatus)
    }
}
finally {
    Pop-Location
}

$checks = [ordered]@{
    requiredProjectFiles = ($missingFiles.Count -eq 0)
    requiredBaselines = ($missingBaselines.Count -eq 0)
    baselineJsonPasses = ($baselineFailures.Count -eq 0)
    rootDirectoryClean = ($unexpectedRootFiles.Count -eq 0)
    artifactsCentralized = ($looseArtifacts.Count -eq 0)
    testsCentralized = (Test-Path -LiteralPath (Join-Path $projectRoot "tests\fixtures"))
    documentationIndexed = (
        (Test-Path -LiteralPath (Join-Path $projectRoot "docs\README.md")) -and
        (Test-Path -LiteralPath (Join-Path $baselineDir "README.md"))
    )
    powershellScriptsParse = ($scriptParseFailures.Count -eq 0)
    centralizedSettingsImplementation = ($settingsReferencesOutsideStore.Count -eq 0)
    clipboardNativeApiBoundary = (
        $clipboardApiReferencesOutsideGateway.Count -eq 0 -and
        $missingGatewayClipboardApis.Count -eq 0
    )
    browserEngineExcluded = ($browserReferences.Count -eq 0)
    deferredFeaturesExcluded = ($deferredCommandReferences.Count -eq 0)
    ahkMigrationBoundaryPreserved = $migrationBoundary
    originalAhkUnchanged = ($actualAhkHash -eq $expectedAhkHash)
    gitRequirement = (-not $RequireGit -or $gitInitialized)
    gitExclusions = (-not $gitInitialized -or $trackedExcluded.Count -eq 0)
    gitWhitespace = (-not $gitInitialized -or $gitWhitespaceClean)
    noUnstagedChanges = (-not $gitInitialized -or $unstagedClean)
    cleanRequirement = (-not $RequireClean -or $repositoryClean)
}
$allPassed = -not ($checks.Values -contains $false)
$report = [ordered]@{
    timestamp = (Get-Date).ToString("o")
    projectRoot = $projectRoot
    originalAhk = [ordered]@{
        path = $OriginalAhkPath
        expectedHash = $expectedAhkHash
        actualHash = $actualAhkHash
    }
    details = [ordered]@{
        missingFiles = $missingFiles
        missingBaselines = $missingBaselines
        baselineFailures = $baselineFailures
        unexpectedRootFiles = $unexpectedRootFiles
        looseArtifacts = @($looseArtifacts | ForEach-Object { $_.Name })
        scriptParseFailures = $scriptParseFailures
        clipboardApiReferencesOutsideGateway = @(
            $clipboardApiReferencesOutsideGateway | ForEach-Object {
                "$($_.Path):$($_.LineNumber):$($_.Line.Trim())"
            }
        )
        missingGatewayClipboardApis = $missingGatewayClipboardApis
        trackedExcluded = $trackedExcluded
        gitInitialized = $gitInitialized
        gitStatus = $gitStatus
    }
    checks = $checks
    allPassed = $allPassed
}
$json = $report | ConvertTo-Json -Depth 10
if (-not $NoWrite) {
    $absoluteOutput = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
        $OutputPath
    }
    else {
        Join-Path $projectRoot $OutputPath
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $absoluteOutput) -Force | Out-Null
    [System.IO.File]::WriteAllText(
        $absoluteOutput, $json, [System.Text.UTF8Encoding]::new($false)
    )
}
$json
if (-not $allPassed) {
    exit 1
}
