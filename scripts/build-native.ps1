param(
    [string]$VisualStudioDirectory = 'C:\BuildTools2022',
    [string]$BuildDirectory,
    [switch]$RestorePackages,
    [string]$NuGetExecutable
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Native application builds require Windows.' }
$repository = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repository 'build-native-production' }
$BuildDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
. (Join-Path $PSScriptRoot 'windows-native-toolchain.ps1')
$toolchain = Import-NativeToolchain $VisualStudioDirectory
$settingsProject = Join-Path $repository 'native\settings\w11widget_native_settings.vcxproj'
if ($RestorePackages) {
    if (-not $NuGetExecutable) { throw 'RestorePackages requires the explicitly selected NuGetExecutable.' }
    Invoke-NativeBuildCommand $NuGetExecutable @('restore', (Join-Path $repository 'native\settings\packages.config'),
        '-PackagesDirectory', (Join-Path $repository 'native\settings\packages'), '-NonInteractive', '-Source', 'https://api.nuget.org/v3/index.json')
}
Invoke-NativeBuildCommand $toolchain.CMake @('-S', (Join-Path $repository 'native'), '-B', $BuildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', ('-DCMAKE_MAKE_PROGRAM=' + $toolchain.Ninja))
Invoke-NativeBuildCommand $toolchain.CMake @('--build', $BuildDirectory)
Invoke-NativeBuildCommand $toolchain.CTest @('--test-dir', $BuildDirectory, '--output-on-failure', '--no-tests=error')
# MSBuild FileTracker excludes headers under LOCALAPPDATA; rebuild avoids stale shared-struct ABI.
Invoke-NativeBuildCommand $toolchain.MSBuild @($settingsProject, '/t:Rebuild', '/m', '/p:Configuration=Release', '/p:Platform=x64', '/v:minimal')
$result = [ordered]@{
    version = ([IO.File]::ReadAllText((Join-Path $repository 'VERSION'))).Trim()
    createdAtUtc = [DateTime]::UtcNow.ToString('o')
    panelExecutable = Join-Path $BuildDirectory 'bin\w11widget_native_panel.exe'
    settingsDirectory = Join-Path $repository 'native\settings\out\x64\Release'
    guiTested = $false
}
foreach ($artifact in @($result.panelExecutable, (Join-Path $result.settingsDirectory 'w11widget_native_settings.exe'))) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Native build artifact missing: $artifact" }
}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BuildDirectory 'build-result.json') -Encoding UTF8
Write-Output 'PASS: native panel/core compilation, CTest and WinUI settings compilation. GUI tests and packaging are separate.'
