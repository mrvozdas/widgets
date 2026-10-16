param([switch]$GuiVisibility)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$measure = Join-Path $PSScriptRoot '..\scripts\measure-process.ps1'
$engine = (Get-Process -Id $PID).Path
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('widget-benchmark-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($temporary) | Out-Null
$sentinel = $null

function Assert($Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}

try {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $engine
    $start.Arguments = '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 30"'
    $start.UseShellExecute = $false
    $sentinel = [Diagnostics.Process]::Start($start)

    $success = Join-Path $temporary 'success.json'
    & $measure -Executable $engine -ArgumentList @('-NoProfile', '-NonInteractive', '-Command',
        'while ($true) { [Math]::Sqrt(123456789) | Out-Null }') `
        -OutputJson $success -DurationSeconds 1 -WarmupSeconds 0 -SampleIntervalMilliseconds 200
    $report = Get-Content -LiteralPath $success -Raw | ConvertFrom-Json
    Assert ($report.status -eq 'completed' -and $report.sampleCount -ge 4) 'actual process yields multiple samples'
    Assert ($report.measuredDurationSeconds -ge 0.8) 'measured duration covers the requested interval'
    Assert ($report.cpu.consumedSeconds -gt 0 -and $report.cpu.percentOfOneLogicalCore -gt 0) 'busy process records CPU consumption'
    Assert ($report.cpu.percentOfMachine -le $report.cpu.percentOfOneLogicalCore) 'machine CPU usage is normalized by logical cores'
    Assert ($report.peak.workingSetBytes -gt 0) 'working set peak is measured'
    Assert ($report.cleanup.exited) 'only the launched process is cleaned up'
    Assert (-not $sentinel.HasExited) 'another process with the same executable remains alive'

    $failure = Join-Path $temporary 'early-exit.json'
    $rejected = $false
    try {
        & $measure -Executable $engine -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'exit 7') `
            -OutputJson $failure -DurationSeconds 1 -WarmupSeconds 2 -SampleIntervalMilliseconds 200
    } catch { $rejected = $true }
    $earlyExit = Get-Content -LiteralPath $failure -Raw | ConvertFrom-Json
    Assert ($rejected -and $earlyExit.status -eq 'failed') 'early process exit cannot produce a successful benchmark'
    Assert ($earlyExit.error -match 'warmup' -and $earlyExit.sampleCount -eq 0) 'failure provenance is retained without invented samples'
    Assert (-not $sentinel.HasExited) 'early-exit handling does not attach to another existing instance'
    $conflicting = Join-Path $temporary 'conflicting-visibility.json'
    $rejected = $false
    try {
        & $measure -Executable $engine -OutputJson $conflicting -HideWindow -RequireHidden
    } catch { $rejected = $_.Exception.Message -match 'either HideWindow or RequireHidden' }
    Assert ($rejected -and -not (Test-Path -LiteralPath $conflicting)) 'conflicting visibility requests fail before launching a process'
    Assert (-not $sentinel.HasExited) 'visibility argument rejection preserves another process'

    if ($GuiVisibility) {
        if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'GUI visibility tests require Windows.' }
        $visibleScript = 'Add-Type -AssemblyName System.Windows.Forms; $form = New-Object Windows.Forms.Form; $form.Text = "Benchmark visibility fixture"; [Windows.Forms.Application]::Run($form)'
        $hidden = Join-Path $temporary 'hidden.json'
        & $measure -Executable $engine -ArgumentList @('-NoProfile', '-NonInteractive', '-STA', '-Command', $visibleScript) `
            -OutputJson $hidden -HideWindow -DurationSeconds 1 -WarmupSeconds 2 -SampleIntervalMilliseconds 200
        $hiddenReport = Get-Content -LiteralPath $hidden -Raw | ConvertFrom-Json
        Assert ($hiddenReport.visibility.confirmedHidden -and $hiddenReport.visibility.ownedWindows.Count -gt 0) 'visible owned HWNDs are hidden and recorded'
        Assert ($hiddenReport.visibility.verifiedSamples -eq $hiddenReport.sampleCount) 'hidden ownership is rechecked at every sample'
        Assert (-not $sentinel.HasExited) 'hiding a fixture does not affect another process'

        $hiddenScript = 'Add-Type -AssemblyName System.Windows.Forms; $form = New-Object Windows.Forms.Form; [void]$form.Handle; [Windows.Forms.Application]::Run()'
        $alreadyHidden = Join-Path $temporary 'already-hidden.json'
        & $measure -Executable $engine -ArgumentList @('-NoProfile', '-NonInteractive', '-STA', '-Command', $hiddenScript) `
            -OutputJson $alreadyHidden -RequireHidden -DurationSeconds 1 -WarmupSeconds 2 -SampleIntervalMilliseconds 200
        $alreadyHiddenReport = Get-Content -LiteralPath $alreadyHidden -Raw | ConvertFrom-Json
        Assert ($alreadyHiddenReport.visibility.confirmedHidden -and $alreadyHiddenReport.visibility.verifiedSamples -eq $alreadyHiddenReport.sampleCount) 'already-hidden owned HWNDs are measured without showing or hiding them'
        Write-Output 'PASS: 4 additional Windows GUI visibility checks.'
    }
    Write-Output 'PASS: benchmark sampling, CPU accounting, isolated cleanup, early-exit and conflicting-visibility rejection (12 checks).'
} finally {
    if ($null -ne $sentinel -and -not $sentinel.HasExited) {
        $sentinel.Kill()
        $sentinel.WaitForExit()
    }
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
