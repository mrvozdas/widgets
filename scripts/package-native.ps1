param(
    [Parameter(Mandatory = $true)][string]$PanelExecutable,
    [Parameter(Mandatory = $true)][string]$SettingsDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$VisualStudioDirectory = 'C:\BuildTools2022',
    [string]$PackagesDirectory,
    [string]$SourceRevision,
    [switch]$VerifyInstaller
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Native packaging requires Windows.' }
$repository = Split-Path -Parent $PSScriptRoot
$version = ([IO.File]::ReadAllText((Join-Path $repository 'VERSION'))).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain three numeric components.' }
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'OutputDirectory must be new; release artifacts are never overwritten.' }
. (Join-Path $PSScriptRoot 'windows-native-toolchain.ps1')
$toolchain = Import-NativeToolchain $VisualStudioDirectory
$settingsExecutable = Join-Path (Resolve-Path -LiteralPath $SettingsDirectory).ProviderPath 'w11widget_native_settings.exe'
if (-not (Test-Path -LiteralPath $settingsExecutable -PathType Leaf) -or
    ([string][Diagnostics.FileVersionInfo]::GetVersionInfo($settingsExecutable).ProductVersion).Trim() -ne $version) {
    throw 'WinUI settings binary ProductVersion does not match VERSION; rebuild before packaging.'
}
$panel = (Resolve-Path -LiteralPath $PanelExecutable).ProviderPath
if (([string][Diagnostics.FileVersionInfo]::GetVersionInfo($panel).ProductVersion).Trim() -ne $version) {
    throw 'Native panel binary ProductVersion does not match VERSION; rebuild before packaging.'
}
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = $panel
$start.Arguments = '--version'
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = New-Object Diagnostics.Process
$process.StartInfo = $start
$started = $false
try {
    if (-not $process.Start()) { throw 'Could not inspect native panel version.' }
    $started = $true
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(5000)) { throw 'Native panel --version did not finish within five seconds.' }
    if ($process.ExitCode -ne 0 -or $stdout.GetAwaiter().GetResult().Trim() -ne $version) {
        throw 'Native panel binary version does not match VERSION; rebuild before packaging.'
    }
} finally {
    if ($started -and -not $process.HasExited) { $process.Kill(); [void]$process.WaitForExit(5000) }
    $process.Dispose()
}
$package = Join-Path $output ('w11widget-' + $version)
& (Join-Path $PSScriptRoot 'package-native-runtime.ps1') -PanelExecutable $PanelExecutable -SettingsDirectory $SettingsDirectory `
    -CrtDirectory $toolchain.Crt -Dumpbin $toolchain.Dumpbin -OutputDirectory $package -SourceRevision $SourceRevision `
    -PackageKind Application -Version $version -PackagesDirectory $PackagesDirectory
$installerBuild = Join-Path $output 'installer-build'
Invoke-NativeBuildCommand $toolchain.CMake @('-S', (Join-Path $repository 'installer'), '-B', $installerBuild, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', ('-DCMAKE_MAKE_PROGRAM=' + $toolchain.Ninja), ('-DW11WIDGET_PACKAGE_DIRECTORY=' + $package))
Invoke-NativeBuildCommand $toolchain.CMake @('--build', $installerBuild)
$installerName = 'w11widget-' + $version + '-installer.exe'
$installer = Join-Path $output $installerName
Copy-Item -LiteralPath (Join-Path $installerBuild ('bin\' + $installerName)) -Destination $installer
$verified = $false
if ($VerifyInstaller) {
    & (Join-Path $PSScriptRoot 'test-installer.ps1') -Installer $installer -PackageDirectory $package `
        -WorkRoot (Join-Path $output 'installer-verification') -Version $version `
        -LiveFixture (Join-Path $installerBuild 'fixture\w11widget.exe') -PackageKind Native
    $verified = $true
}
$portable = Join-Path $output ('w11widget-' + $version + '-windows-x64.zip')
# Normalize entry names explicitly: .NET Framework's CreateFromDirectory can emit backslashes.
# Enumerate with Force because Compress-Archive silently omits hidden runtime files.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$expected = @{}
$archive = [IO.Compression.ZipFile]::Open($portable, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in (Get-ChildItem -LiteralPath $package -Recurse -File -Force)) {
        $name = ([IO.Path]::GetFileName($package) + '/' + $file.FullName.Substring($package.Length + 1)).Replace('\', '/')
        $expected[$name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
        $source = [IO.File]::OpenRead($file.FullName)
        $destination = $null
        try { $destination = $entry.Open(); $source.CopyTo($destination) }
        finally { if ($null -ne $destination) { $destination.Dispose() }; $source.Dispose() }
    }
} finally { $archive.Dispose() }
$archive = [IO.Compression.ZipFile]::OpenRead($portable)
$seen = @{}
try {
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        if (-not $expected.ContainsKey($entry.FullName) -or $seen.ContainsKey($entry.FullName)) { throw 'Portable ZIP contains unexpected or duplicate entries.' }
        $stream = $entry.Open()
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
        finally { $algorithm.Dispose(); $stream.Dispose() }
        if ($hash -ne $expected[$entry.FullName]) { throw "Portable ZIP file hash mismatch: $($entry.FullName)" }
        $seen[$entry.FullName] = $true
    }
    if ($seen.Count -ne $expected.Count) { throw 'Portable ZIP is missing runtime files.' }
} finally { $archive.Dispose() }
$artifacts = @($installer, $portable) | ForEach-Object {
    [pscustomobject][ordered]@{ name = [IO.Path]::GetFileName($_); bytes = (Get-Item -LiteralPath $_).Length; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() }
}
[ordered]@{ version = $version; sourceRevision = $SourceRevision; installerVerified = $verified; guiTested = $false; artifacts = @($artifacts) } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'release-manifest.json') -Encoding UTF8
$artifacts | ForEach-Object { $_.sha256 + '  ' + $_.name } | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS') -Encoding ASCII
Write-Output "Native package completed: $output (installer verification: $verified; GUI verification separate)."
