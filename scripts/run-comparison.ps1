param(
    [Parameter(Mandatory = $true)][string]$QtExecutable,
    [Parameter(Mandatory = $true)][string]$NativeExecutable,
    [string]$SettingsExecutable,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateRange(10, 300)][int]$DurationSeconds = 60,
    [switch]$NativeFirst,
    [switch]$Hidden,
    [switch]$NativeProduction
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$profilePath = Join-Path $env:LOCALAPPDATA 'w11widget\settings.json'
$before = if (Test-Path $profilePath) { (Get-FileHash $profilePath -Algorithm SHA256).Hash } else { $null }
$statusPath = Join-Path $OutputDirectory 'session-status.json'
$panels = @(@{ name = 'qt'; executable = $QtExecutable }, @{ name = 'native'; executable = $NativeExecutable })
if ($NativeFirst) { $panels = @($panels[1], $panels[0]) }
$status = [ordered]@{ status = 'running'; stage = $panels[0].name; panelOrder = @($panels.name); visibilityMode = $(if ($Hidden) { 'hidden' } else { 'visible' }); sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId; profileUnchanged = $null; error = $null }
$status.nativeMode = if ($NativeProduction) { 'production' } else { 'reference-prototype' }
function Save-Status { $status | ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8 }
Save-Status
try {
    if ($status.sessionId -eq 0) { throw 'Run the comparison in the interactive desktop session, not SSH session zero.' }
    $arguments = @('--benchmark', '--duration-seconds', [string]($DurationSeconds + 30))
    $nativeArguments = $arguments
    $settingsArguments = @()
    if ($NativeProduction) {
        $isolated = [IO.Path]::GetFullPath((Join-Path $OutputDirectory 'comparison-settings.json'))
        if (Test-Path -LiteralPath $isolated) { throw 'Use a new production measurement directory.' }
        $widgets = @(@{ instance_id='countdown-1'; type_id='countdown'; enabled=$true; visible=$true; width_dip=120;
            settings=@{label='Countdown';target_time='18:00';time_zone='Europe/Istanbul';daily_repeat=$true;target_date=''} })
        $index = 0
        foreach ($zone in @('UTC', 'America/New_York', 'Europe/Istanbul')) {
            $index++
            $widgets += @{ instance_id=('clock-' + $index); type_id='world-clock'; enabled=$true; visible=$true; width_dip=120;
                settings=@{label='';time_zone=$zone;show_date=$false} }
        }
        $fixture = @{schema_version=2;panel=@{monitor='primary';side='left';width_dip=620;offset_dip=0;opacity=1.0;hide_for_fullscreen=$false};widgets=$widgets}
        [IO.File]::WriteAllText($isolated, ($fixture | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
        # The production close action hides to tray. Its bounded lifetime ends within cleanup's grace period.
        $nativeArguments = @('--settings-file', $isolated, '--duration-seconds', [string]($DurationSeconds + 8))
        $settingsArguments = @('--settings-file', $isolated, '--duration-seconds', '68')
    }
    foreach ($panel in $panels) {
        $status.stage = $panel.name; Save-Status
        $measurement = @{
            Executable = $panel.executable
            ArgumentList = $arguments
            DurationSeconds = $DurationSeconds
            OutputJson = Join-Path $OutputDirectory ($panel.name + '-panel.json')
            CapturePng = Join-Path $OutputDirectory ($panel.name + '-panel.png')
        }
        if ($panel.name -eq 'native') { $measurement.ArgumentList = $nativeArguments }
        if ($Hidden) {
            if ($panel.name -eq 'qt') {
                $measurement.ArgumentList = @($arguments) + '--benchmark-hidden'
                $measurement.RequireHidden = $true
                $measurement.Remove('CapturePng')
            } else {
                $measurement.HideWindow = $true
            }
        }
        & "$PSScriptRoot\measure-process.ps1" @measurement
    }
    if ($SettingsExecutable) {
        $status.stage = 'settings'; Save-Status
        & "$PSScriptRoot\measure-process.ps1" -Executable $SettingsExecutable -ArgumentList $settingsArguments -DurationSeconds 60 `
            -OutputJson (Join-Path $OutputDirectory 'native-settings.json') -CapturePng (Join-Path $OutputDirectory 'native-settings.png')
    }
    $status.status = 'completed'
} catch {
    $status.status = 'failed'
    $status.error = $_.Exception.Message
    throw
} finally {
    $after = if (Test-Path $profilePath) { (Get-FileHash $profilePath -Algorithm SHA256).Hash } else { $null }
    $status.profileUnchanged = $before -eq $after
    if (-not $status.profileUnchanged) { $status.status = 'failed'; $status.error = 'User settings changed during comparison.' }
    Save-Status
}
