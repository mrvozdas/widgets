param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [string[]]$ArgumentList = @(),
    [Parameter(Mandatory = $true)][string]$OutputJson,
    [string]$CapturePng,
    [switch]$HideWindow,
    [switch]$RequireHidden,
    [ValidateRange(1, 3600)][int]$DurationSeconds = 60,
    [ValidateRange(0, 300)][int]$WarmupSeconds = 5,
    [ValidateRange(100, 10000)][int]$SampleIntervalMilliseconds = 1000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($HideWindow -and $RequireHidden) { throw 'Choose either HideWindow or RequireHidden.' }
if (($HideWindow -or $RequireHidden) -and [Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Owned-window visibility verification requires Windows.'
}
if ($RequireHidden -and $CapturePng) { throw 'RequireHidden cannot capture a visible window.' }

if (($HideWindow -or $RequireHidden) -and -not ('W11WidgetOwnedWindows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class W11WidgetOwnedWindows {
    private delegate bool Callback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr window, int command);
    public static IntPtr[] Owned(int processId, bool visibleOnly) {
        var result = new List<IntPtr>();
        Callback callback = delegate(IntPtr window, IntPtr unused) {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == processId && (!visibleOnly || IsWindowVisible(window))) result.Add(window);
            return true;
        };
        if (!EnumWindows(callback, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
        return result.ToArray();
    }
    public static void Hide(int processId, IntPtr[] windows) {
        foreach (var window in windows) {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != processId) throw new InvalidOperationException("Window owner changed before hiding.");
            ShowWindow(window, 0);
        }
    }
}
'@
}

function ConvertTo-NativeArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    # Windows CRT argument parsing doubles backslashes before quotes and at the end.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Read-ProcessSample($Process, $Timer) {
    $Process.Refresh()
    if ($Process.HasExited) { throw "The launched process exited before measurement finished (exit code $($Process.ExitCode))." }
    if ($HideWindow -or $RequireHidden) {
        if ([W11WidgetOwnedWindows]::Owned($Process.Id, $false).Length -eq 0 -or
            [W11WidgetOwnedWindows]::Owned($Process.Id, $true).Length -ne 0) {
            throw 'The launched process did not remain hidden during measurement.'
        }
        $report.visibility.verifiedSamples++
    }
    return [pscustomobject]@{
        elapsedSeconds = [Math]::Round($Timer.Elapsed.TotalSeconds, 6)
        cpuTotalSeconds = $Process.TotalProcessorTime.TotalSeconds
        privateBytes = $Process.PrivateMemorySize64
        workingSetBytes = $Process.WorkingSet64
        handles = $Process.HandleCount
        threads = $Process.Threads.Count
    }
}

$executablePath = (Resolve-Path -LiteralPath $Executable).ProviderPath
if (-not [IO.File]::Exists($executablePath)) { throw 'Executable must be a file.' }
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputJson)
if ([string]::Equals($outputPath, $executablePath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The measurement output must not overwrite the executable.'
}
$outputDirectory = [IO.Path]::GetDirectoryName($outputPath)
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$samples = New-Object 'System.Collections.Generic.List[object]'
$process = $null
$startedAt = $null
$measurementError = $null
$report = [ordered]@{
    schemaVersion = 1
    status = 'failed'
    scope = 'launched-primary-process-only'
    executable = $executablePath
    executableSha256 = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    arguments = @($ArgumentList)
    platform = [Environment]::OSVersion.VersionString
    logicalProcessorCount = [Environment]::ProcessorCount
    requestedDurationSeconds = $DurationSeconds
    warmupSeconds = $WarmupSeconds
    sampleIntervalMilliseconds = $SampleIntervalMilliseconds
    processId = $null
    processStartTimeUtc = $null
    sampleCount = 0
    measuredDurationSeconds = 0
    cpu = $null
    first = $null
    last = $null
    peak = $null
    error = $null
    capture = $null
    visibility = [ordered]@{ requested = $(if ($HideWindow) { 'hide-owned-windows' } elseif ($RequireHidden) { 'require-already-hidden' } else { 'unchanged' }); confirmedHidden = $false; ownedWindows = @(); verifiedSamples = 0 }
    cleanup = [ordered]@{ gracefulCloseRequested = $false; forcedTermination = $false; exited = $false; error = $null }
    samples = @()
}

try {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $executablePath
    $start.WorkingDirectory = [IO.Path]::GetDirectoryName($executablePath)
    $start.UseShellExecute = $false
    # ArgumentList preserves argv on modern PowerShell; Windows PowerShell 5.1 needs CRT quoting.
    if ($null -ne $start.PSObject.Properties['ArgumentList']) {
        foreach ($argument in $ArgumentList) { $start.ArgumentList.Add($argument) }
    } else {
        $start.Arguments = (($ArgumentList | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    }
    $process = [System.Diagnostics.Process]::Start($start)
    $startedAt = $process.StartTime.ToUniversalTime()
    $report.processId = $process.Id
    $report.processStartTimeUtc = $startedAt.ToString('o')
    if ($process.WaitForExit($WarmupSeconds * 1000)) {
        throw "The launched process exited during warmup (exit code $($process.ExitCode)); an existing single-instance application is not measured."
    }
    if ($CapturePng) {
        $capturePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CapturePng)
        if ([string]::Equals($capturePath, $outputPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Screenshot and measurement output must use different paths.'
        }
        $current = Get-Process -Id $process.Id -ErrorAction Stop
        if ($current.StartTime.ToUniversalTime() -ne $startedAt) { throw 'Launched process identity changed before capture.' }
        $captured = & (Join-Path $PSScriptRoot 'capture-window.ps1') -ProcessId $process.Id -OutputPng $capturePath
        $current.Refresh()
        if ($current.HasExited -or $current.StartTime.ToUniversalTime() -ne $startedAt) {
            throw 'Launched process identity could not be confirmed after capture.'
        }
        $report.capture = $captured
    }
    if ($HideWindow -or $RequireHidden) {
        $current = Get-Process -Id $process.Id -ErrorAction Stop
        if ($current.StartTime.ToUniversalTime() -ne $startedAt) { throw 'Launched process identity changed before visibility verification.' }
        $owned = @([W11WidgetOwnedWindows]::Owned($process.Id, $false))
        if ($owned.Length -eq 0) { throw 'The launched process has no owned window to verify.' }
        if ($HideWindow) {
            $visible = @([W11WidgetOwnedWindows]::Owned($process.Id, $true))
            if ($visible.Length -eq 0) { throw 'HideWindow requires a visible window owned by the launched process.' }
            [W11WidgetOwnedWindows]::Hide($process.Id, $visible)
            Start-Sleep -Milliseconds 200
        }
        $current.Refresh()
        if ($current.HasExited -or $current.StartTime.ToUniversalTime() -ne $startedAt -or
            [W11WidgetOwnedWindows]::Owned($process.Id, $true).Length -ne 0) {
            throw 'The exact launched process windows could not be confirmed hidden.'
        }
        $report.visibility.ownedWindows = @($owned | ForEach-Object { '0x' + $_.ToInt64().ToString('x') })
        $report.visibility.confirmedHidden = $true
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $samples.Add((Read-ProcessSample $process $timer))
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $remaining = [Math]::Max(1, [Math]::Ceiling(($DurationSeconds - $timer.Elapsed.TotalSeconds) * 1000))
        Start-Sleep -Milliseconds ([int][Math]::Min($SampleIntervalMilliseconds, $remaining))
        $samples.Add((Read-ProcessSample $process $timer))
    }
    $timer.Stop()
    $report.status = 'completed'
} catch {
    $measurementError = $_
    $report.error = $_.Exception.Message
} finally {
    if ($null -ne $process) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                # Never select by name or kill a pre-existing instance or a reused PID.
                $current = Get-Process -Id $process.Id -ErrorAction Stop
                if ($null -eq $startedAt -or $current.StartTime.ToUniversalTime() -ne $startedAt) {
                    throw 'Launched process identity could not be confirmed; cleanup did not terminate it.'
                }
                $report.cleanup.gracefulCloseRequested = $current.CloseMainWindow()
                if (-not $current.WaitForExit(5000)) {
                    $current.Kill()
                    $report.cleanup.forcedTermination = $true
                    if (-not $current.WaitForExit(5000)) { throw 'Launched process did not exit after cleanup.' }
                }
            }
            $process.Refresh()
            $report.cleanup.exited = $process.HasExited
        } catch {
            $report.cleanup.error = $_.Exception.Message
            $report.status = 'failed'
            if ($null -eq $measurementError) { $measurementError = $_ }
        }
        $process.Dispose()
    }
    $report.sampleCount = $samples.Count
    $report.samples = @($samples.ToArray())
    if ($samples.Count -gt 0) {
        $first = $samples[0]
        $last = $samples[$samples.Count - 1]
        $elapsed = $last.elapsedSeconds - $first.elapsedSeconds
        $report.first = $first
        $report.last = $last
        $report.measuredDurationSeconds = [Math]::Round($elapsed, 6)
        $report.peak = [ordered]@{}
        foreach ($metric in @('privateBytes', 'workingSetBytes', 'handles', 'threads')) {
            $report.peak[$metric] = ($samples | Measure-Object -Property $metric -Maximum).Maximum
        }
        if ($elapsed -gt 0) {
            $cpuSeconds = $last.cpuTotalSeconds - $first.cpuTotalSeconds
            $singleCorePercent = 100 * $cpuSeconds / $elapsed
            $report.cpu = [ordered]@{
                consumedSeconds = $cpuSeconds
                percentOfOneLogicalCore = [Math]::Round($singleCorePercent, 6)
                percentOfMachine = [Math]::Round($singleCorePercent / $report.logicalProcessorCount, 6)
            }
        }
    }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outputPath -Encoding UTF8
}

if ($null -ne $measurementError) { throw $measurementError }
Write-Output "Measurement completed: $outputPath ($($report.sampleCount) samples, primary process only)."
if ($report.cleanup.forcedTermination) { Write-Output 'Cleanup force-terminated only the exact process launched by this command.' }
