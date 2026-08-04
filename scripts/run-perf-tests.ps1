[CmdletBinding()]
param(
    [int]$ColdSamples = 5,
    [int]$HotSamples = 40,
    [int]$InputSamples = 60,
    [int]$IdleSeconds = 5,
    [string]$BuildSubdirectory = "build\release",
    [string]$ServerName = "ScratchEditor.Perf.Validation",
    [string]$ArtifactPrefix = "perf-results"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot $BuildSubdirectory
$editorExe = Join-Path $buildDir "ScratchEditor.exe"
$perfExe = Join-Path $buildDir "ScratchEditorPerf.exe"
$qtBin = Join-Path $projectRoot ".tools\Qt\6.10.2\mingw_64\bin"
$mingwBin = Join-Path $projectRoot ".tools\Qt\Tools\mingw1310_64\bin"
$artifactDir = Join-Path $projectRoot "artifacts"
$pipeName = $ServerName
$env:SCRATCHEDITOR_SERVER_NAME = $ServerName
$env:PATH = "$qtBin;$mingwBin;$env:PATH"

function Send-IpcCommand {
    param(
        [Parameter(Mandatory)] [string]$Command,
        [int]$TimeoutMs = 3000
    )

    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".",
        $pipeName,
        [System.IO.Pipes.PipeDirection]::InOut
    )
    try {
        $pipe.Connect($TimeoutMs)
        $encoding = [System.Text.UTF8Encoding]::new($false)
        $writer = [System.IO.StreamWriter]::new($pipe, $encoding, 4096, $true)
        $reader = [System.IO.StreamReader]::new($pipe, $encoding, $false, 4096, $true)
        try {
            $request = @{ command = $Command } | ConvertTo-Json -Compress
            $writer.WriteLine($request)
            $writer.Flush()
            $line = $reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($line)) {
                throw "IPC returned an empty response for '$Command'."
            }
            return $line | ConvertFrom-Json
        }
        finally {
            $reader.Dispose()
            $writer.Dispose()
        }
    }
    finally {
        $pipe.Dispose()
    }
}

function Stop-TestInstance {
    try {
        $status = Send-IpcCommand -Command "status" -TimeoutMs 150
        if (-not $status.testMode) {
            throw "A non-test ScratchEditor instance is already running; refusing to stop it."
        }
        $null = Send-IpcCommand -Command "quit" -TimeoutMs 1000
        for ($attempt = 0; $attempt -lt 100; $attempt++) {
            Start-Sleep -Milliseconds 10
            try {
                $null = Send-IpcCommand -Command "status" -TimeoutMs 10
            }
            catch {
                return
            }
        }
        throw "The prior test instance did not exit after the quit command."
    }
    catch [System.TimeoutException] {
    }
    catch [System.IO.IOException] {
    }
}

function Start-And-WaitReady {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $editorExe
    $startInfo.Arguments = "--background --test-mode"
    $startInfo.UseShellExecute = $false
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $status = $null
    while ($stopwatch.ElapsedMilliseconds -lt 3000) {
        try {
            $candidate = Send-IpcCommand -Command "status" -TimeoutMs 30
            if ($candidate.ready -and [int]$candidate.pid -eq $process.Id) {
                $status = $candidate
                break
            }
        }
        catch {
        }
        Start-Sleep -Milliseconds 2
    }
    $stopwatch.Stop()
    if ($null -eq $status) {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id
        }
        throw "ScratchEditor did not become ready within 3000 ms."
    }
    return [pscustomobject]@{
        Process = $process
        ExternalReadyMs = $stopwatch.Elapsed.TotalMilliseconds
        Status = $status
    }
}

if (-not (Test-Path -LiteralPath $editorExe) -or -not (Test-Path -LiteralPath $perfExe)) {
    throw "Release binaries are missing. Run scripts/build.ps1 first."
}

Stop-TestInstance
$warmupInfo = [System.Diagnostics.ProcessStartInfo]::new()
$warmupInfo.FileName = $env:ComSpec
$warmupInfo.Arguments = "/d /c exit 0"
$warmupInfo.UseShellExecute = $false
$warmupInfo.CreateNoWindow = $true
$warmupProcess = [System.Diagnostics.Process]::Start($warmupInfo)
$warmupProcess.WaitForExit(2000) | Out-Null
$deploymentWarmup = Start-And-WaitReady
$deploymentWarmupMs = $deploymentWarmup.ExternalReadyMs
$null = Send-IpcCommand -Command "quit"
if (-not $deploymentWarmup.Process.WaitForExit(2000)) {
    throw "Deployment warm-up process $($deploymentWarmup.Process.Id) did not exit."
}
Start-Sleep -Milliseconds 100
$coldResults = @()
$coldInternalResults = @()
$lastColdStatus = $null
for ($sample = 0; $sample -lt $ColdSamples; $sample++) {
    $started = Start-And-WaitReady
    $coldResults += $started.ExternalReadyMs
    $coldInternalResults += [double]$started.Status.startupMs
    $lastColdStatus = $started.Status
    $null = Send-IpcCommand -Command "quit"
    if (-not $started.Process.WaitForExit(2000)) {
        throw "Cold-start sample process $($started.Process.Id) did not exit."
    }
    Start-Sleep -Milliseconds 100
}

$resident = Start-And-WaitReady
Start-Sleep -Milliseconds 250
$resident.Process.Refresh()
$workingSetMb = $resident.Process.WorkingSet64 / 1MB
$cpuStart = $resident.Process.TotalProcessorTime.TotalSeconds
$idleWatch = [System.Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $IdleSeconds
$idleWatch.Stop()
$resident.Process.Refresh()
$cpuDelta = $resident.Process.TotalProcessorTime.TotalSeconds - $cpuStart
$idleCpuPercent = $cpuDelta / $idleWatch.Elapsed.TotalSeconds /
    [Environment]::ProcessorCount * 100.0

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$perfInfo = [System.Diagnostics.ProcessStartInfo]::new()
$perfInfo.FileName = $perfExe
$perfInfo.Arguments = "--hot-samples $HotSamples --input-samples $InputSamples"
$perfInfo.UseShellExecute = $false
$perfInfo.CreateNoWindow = $true
$perfInfo.RedirectStandardOutput = $true
$perfInfo.RedirectStandardError = $true
$perfInfo.StandardOutputEncoding = $utf8NoBom
$perfInfo.StandardErrorEncoding = $utf8NoBom
$perfProcess = [System.Diagnostics.Process]::Start($perfInfo)
$perfOutputTask = $perfProcess.StandardOutput.ReadToEndAsync()
$perfErrorTask = $perfProcess.StandardError.ReadToEndAsync()
$perfProcess.WaitForExit()
$perfJson = $perfOutputTask.Result
$perfError = $perfErrorTask.Result
if ($perfProcess.ExitCode -ne 0) {
    throw "ScratchEditorPerf failed with exit code $($perfProcess.ExitCode): $perfError"
}
if (-not [string]::IsNullOrWhiteSpace($perfError)) {
    Write-Host $perfError.TrimEnd()
}
$perf = $perfJson | ConvertFrom-Json
$finalStatus = Send-IpcCommand -Command "status"

$coldMaximum = ($coldResults | Measure-Object -Maximum).Maximum
$coldAverage = ($coldResults | Measure-Object -Average).Average
$checks = [ordered]@{
    coldStart = ($coldMaximum -le 300.0)
    hotWake = [bool]$perf.hotWake.passed
    inputFrame = [bool]$perf.largeDocumentInput.passed
    idleCpu = ($idleCpuPercent -le 0.5)
    workingSet = ($workingSetMb -le 80.0)
    animation = (
        $perf.animation.ok -and
        $perf.animation.fps -ge 55.0 -and
        $perf.animation.p95FrameMs -le 20.0
    )
    largeDocument = (
        $perf.largeDocument.ok -and
        $perf.largeDocument.characters -eq 100000 -and
        $perf.largeDocument.selectionInvoked -and
        $perf.largeDocument.scrollInvoked -and
        $perf.largeDocument.selectionMs -le 16.667 -and
        $perf.largeDocument.scrollMs -le 16.667
    )
    ime = (
        $perf.ime.ok -and
        $perf.ime.composingObserved -and
        $perf.ime.committedCjk -and
        $perf.ime.compositionEnded -and
        $perf.systemIme.passed
    )
    cjkAndHighDpi = (
        $finalStatus.cjkGlyphAvailable -and
        $finalStatus.devicePixelRatio -gt 0
    )
    windowContract = (
        $finalStatus.alwaysOnTop -and
        $finalStatus.frameless -and
        $finalStatus.minimumWidth -eq 500 -and
        $finalStatus.minimumHeight -eq 320
    )
    darkFirstFrame = ($finalStatus.firstFrameColor -eq "#252525")
}
$allPassed = -not ($checks.Values -contains $false)

$report = [ordered]@{
    timestamp = (Get-Date).ToString("o")
    machine = [ordered]@{
        os = [Environment]::OSVersion.VersionString
        logicalProcessors = [Environment]::ProcessorCount
        qtVersion = "6.10.2"
        compiler = "MinGW 13.1"
    }
    coldStart = [ordered]@{
        deploymentWarmupMs = $deploymentWarmupMs
        measurement = "fresh process with warm filesystem cache"
        samplesMs = $coldResults
        internalReadySamplesMs = $coldInternalResults
        averageMs = $coldAverage
        maximumMs = $coldMaximum
        thresholdMs = 300.0
    }
    idle = [ordered]@{
        seconds = $idleWatch.Elapsed.TotalSeconds
        normalizedCpuPercent = $idleCpuPercent
        cpuThresholdPercent = 0.5
        workingSetMb = $workingSetMb
        workingSetThresholdMb = 80.0
    }
    perf = $perf
    finalStatus = $finalStatus
    checks = $checks
    allPassed = $allPassed
}

$null = Send-IpcCommand -Command "quit"
if (-not $resident.Process.WaitForExit(2000)) {
    throw "Resident test process $($resident.Process.Id) did not exit."
}

New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
$artifactPath = Join-Path $artifactDir (
    "$ArtifactPrefix-{0}.json" -f (Get-Date -Format "yyyyMMdd-HHmmss")
)
$reportJson = $report | ConvertTo-Json -Depth 12
[System.IO.File]::WriteAllText($artifactPath, $reportJson, [System.Text.UTF8Encoding]::new($false))
$reportJson
Write-Host "Perf report: $artifactPath"

if (-not $allPassed) {
    exit 1
}
