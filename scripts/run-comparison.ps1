param(
    [Parameter(Mandatory = $true)][string]$QtExecutable,
    [Parameter(Mandatory = $true)][string]$NativeExecutable,
    [string]$SettingsExecutable,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateRange(10, 300)][int]$DurationSeconds = 60,
    [switch]$NativeFirst,
    [switch]$Hidden
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
function Save-Status { $status | ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8 }
Save-Status
try {
    if ($status.sessionId -eq 0) { throw 'Run the comparison in the interactive desktop session, not SSH session zero.' }
    $arguments = @('--benchmark', '--duration-seconds', [string]($DurationSeconds + 30))
    foreach ($panel in $panels) {
        $status.stage = $panel.name; Save-Status
        $measurement = @{
            Executable = $panel.executable
            ArgumentList = $arguments
            DurationSeconds = $DurationSeconds
            OutputJson = Join-Path $OutputDirectory ($panel.name + '-panel.json')
            CapturePng = Join-Path $OutputDirectory ($panel.name + '-panel.png')
        }
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
        & "$PSScriptRoot\measure-process.ps1" -Executable $SettingsExecutable -DurationSeconds 60 `
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
