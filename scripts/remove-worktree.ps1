[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$WorktreePath
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([System.IO.Path]::IsPathRooted($WorktreePath)) {
    $candidate = [System.IO.Path]::GetFullPath($WorktreePath)
}
else {
    $candidate = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $WorktreePath))
}
$mainRoot = [System.IO.Path]::GetFullPath($projectRoot)

if ($candidate.TrimEnd('\') -eq $mainRoot.TrimEnd('\')) {
    throw "Refusing to remove the main worktree: $mainRoot"
}

$registeredWorktrees = @(
    & git -C $projectRoot worktree list --porcelain |
        Where-Object { $_ -like "worktree *" } |
        ForEach-Object { [System.IO.Path]::GetFullPath($_.Substring(9)) }
)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to query registered Git worktrees."
}
if ($candidate -notin $registeredWorktrees) {
    throw "Path is not a registered worktree: $candidate"
}

$toolsPath = Join-Path $candidate ".tools"
$toolsItem = Get-Item -LiteralPath $toolsPath -Force -ErrorAction SilentlyContinue
if ($toolsItem) {
    if ($toolsItem.LinkType -ne "Junction") {
        throw "Refusing to remove a worktree whose .tools is not a junction: $toolsPath"
    }

    $linkTarget = @($toolsItem.Target)[0]
    $targetPath = [System.IO.Path]::GetFullPath([string]$linkTarget)
    $requiredTool = Join-Path $targetPath "Qt\Tools\CMake_64\bin\cmake.exe"
    if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) {
        throw "Shared toolchain target is missing or incomplete: $targetPath"
    }

    # Windows PowerShell 5.1 can fail or behave inconsistently when Remove-Item
    # targets a directory junction. The non-recursive .NET API removes only the
    # reparse point and cannot traverse into the shared target.
    [System.IO.Directory]::Delete($toolsPath, $false)
    if (Test-Path -LiteralPath $toolsPath) {
        throw "Failed to unlink the worktree .tools junction: $toolsPath"
    }
    if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) {
        throw "Shared toolchain disappeared after unlinking; Git removal was not attempted: $targetPath"
    }
}

& git -C $projectRoot worktree remove $candidate
if ($LASTEXITCODE -ne 0) {
    throw "git worktree remove failed with exit code $LASTEXITCODE"
}

Write-Host "Worktree removed safely: $candidate"
