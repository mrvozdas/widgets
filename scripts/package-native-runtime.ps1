param(
    [Parameter(Mandatory = $true)][string]$PanelExecutable,
    [Parameter(Mandatory = $true)][string]$SettingsDirectory,
    [Parameter(Mandatory = $true)][string]$CrtDirectory,
    [Parameter(Mandatory = $true)][string]$Dumpbin,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$SourceRevision,
    [ValidateSet('Prototype', 'Application')][string]$PackageKind = 'Prototype',
    [string]$Version,
    [string]$PackagesDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or -not [Environment]::Is64BitProcess) {
    throw 'Native prototype packaging requires 64-bit Windows PowerShell and the x64 build outputs.'
}
if ($PackageKind -eq 'Application' -and $Version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Application packaging requires the central three-component VERSION value.'
}

function Assert-X64Image([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE image: $Path" }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw "Invalid PE header: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "The package only supports x64 PE images: $Path"
        }
    } finally { $reader.Dispose() }
}

function Copy-VerifiedFile([string]$Source, [string]$Destination) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination
    if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash) {
        throw "Copied file hash mismatch: $Destination"
    }
}

function Get-RelativePackagePath([string]$Path) {
    return $Path.Substring($output.Length + 1).Replace('\', '/')
}

$panel = (Resolve-Path -LiteralPath $PanelExecutable).ProviderPath
$settings = (Resolve-Path -LiteralPath $SettingsDirectory).ProviderPath.TrimEnd('\', '/')
$crt = (Resolve-Path -LiteralPath $CrtDirectory).ProviderPath.TrimEnd('\', '/')
$dumpbinPath = (Resolve-Path -LiteralPath $Dumpbin).ProviderPath
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory).TrimEnd('\', '/')
if (Test-Path -LiteralPath $output) { throw 'OutputDirectory must be new; existing builds and installations are never overwritten.' }
foreach ($inputDirectory in @($settings, $crt)) {
    if ($output.StartsWith($inputDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputDirectory must be separate from the settings and CRT source directories.'
    }
}
if (-not [IO.File]::Exists($panel) -or -not [IO.File]::Exists($dumpbinPath) -or -not [IO.Directory]::Exists($settings)) {
    throw 'PanelExecutable, SettingsDirectory and Dumpbin must identify existing build artifacts/tools.'
}
if (-not [IO.File]::Exists((Join-Path $settings 'w11widget_native_settings.exe'))) {
    throw 'SettingsDirectory does not contain w11widget_native_settings.exe.'
}
# Only the redistributable CRT is eligible; never harvest runtime DLLs from System32 or compiler/bin.
if ($crt -notmatch '(?i)\\VC\\Redist\\MSVC\\[^\\]+\\x64\\Microsoft\.VC143\.CRT$') {
    throw 'CrtDirectory must be the installed VS 2022 VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT directory.'
}
foreach ($required in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (-not [IO.File]::Exists((Join-Path $crt $required))) { throw "Missing redistributable CRT file: $required" }
}
$sourceItems = @(Get-ChildItem -LiteralPath $settings -Recurse -Force)
if (@($sourceItems | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
    throw 'Settings output must not contain symbolic links or junctions.'
}
$sourceFiles = @($sourceItems | Where-Object {
    -not $_.PSIsContainer -and $_.Extension -ine '.pdb' -and $_.Name -notin @('w11widget_native_panel.exe', 'w11widget.exe')
})
foreach ($reserved in @('PACKAGING-INCOMPLETE.txt', 'package-manifest.json', 'SHA256SUMS', 'PROTOTYPE.txt', 'APPLICATION.txt', 'THIRD-PARTY-NOTICES.txt')) {
    if (Test-Path -LiteralPath (Join-Path $settings $reserved)) { throw 'SettingsDirectory must be a build output, not a previously staged prototype package.' }
}
if (@($sourceFiles | Where-Object { $_.Extension -ieq '.pri' }).Count -eq 0) {
    throw 'Settings output has no PRI resources; build the complete self-contained WinUI output first.'
}
Assert-X64Image $panel
$crtFiles = @(Get-ChildItem -LiteralPath $crt -File -Filter '*.dll')
foreach ($file in $crtFiles) { Assert-X64Image $file.FullName }

$licenseFiles = @()
if ($PackageKind -eq 'Application') {
    $repository = Split-Path -Parent $PSScriptRoot
    if (-not $PackagesDirectory) { $PackagesDirectory = Join-Path $repository 'native\settings\packages' }
    $packagesRoot = (Resolve-Path -LiteralPath $PackagesDirectory).ProviderPath.TrimEnd('\', '/')
    [xml]$pins = Get-Content -LiteralPath (Join-Path $repository 'native\settings\packages.config') -Raw
    # These package notices accompany the deployed runtime and generated C++/WinRT code.
    foreach ($id in @('Microsoft.WindowsAppSDK.Base', 'Microsoft.WindowsAppSDK.Foundation',
        'Microsoft.WindowsAppSDK.InteractiveExperiences', 'Microsoft.WindowsAppSDK.WinUI',
        'Microsoft.Windows.CppWinRT', 'Microsoft.Web.WebView2')) {
        $pin = @($pins.packages.package | Where-Object { $_.id -eq $id })
        if ($pin.Count -ne 1) { throw "Expected one pinned package for license provenance: $id" }
        $packageName = $id + '.' + $pin[0].version
        $packageDirectory = Join-Path $packagesRoot $packageName
        if (-not [IO.Directory]::Exists($packageDirectory)) { throw "Restore the pinned package before staging its license: $packageName" }
        $documents = @(Get-ChildItem -LiteralPath $packageDirectory -File -Force | Where-Object { $_.Name -match '^(LICENSE|NOTICE)(\.[A-Za-z0-9]+)?$' })
        if (@($documents | Where-Object { $_.Name -match '^LICENSE(\.|$)' }).Count -eq 0) {
            throw "Pinned package has no root license document: $packageName"
        }
        foreach ($document in $documents) {
            if ($document.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'License source must not be a symbolic link.' }
            $licenseFiles += [pscustomobject]@{ package = $packageName; source = $document.FullName; path = 'licenses/' + $packageName + '/' + $document.Name }
        }
    }
}

[IO.Directory]::CreateDirectory($output) | Out-Null
$incomplete = Join-Path $output 'PACKAGING-INCOMPLETE.txt'
'Packaging has not finished. Do not deploy this directory.' | Set-Content -LiteralPath $incomplete -Encoding UTF8
foreach ($file in $sourceFiles) {
    Copy-VerifiedFile $file.FullName (Join-Path $output $file.FullName.Substring($settings.Length + 1))
}
foreach ($license in $licenseFiles) { Copy-VerifiedFile $license.source (Join-Path $output $license.path) }
$entryPoint = if ($PackageKind -eq 'Application') { 'w11widget.exe' } else { 'w11widget_native_panel.exe' }
Copy-VerifiedFile $panel (Join-Path $output $entryPoint)
foreach ($file in $crtFiles) {
    $destination = Join-Path $output $file.Name
    if (Test-Path -LiteralPath $destination) {
        $existing = [Diagnostics.FileVersionInfo]::GetVersionInfo($destination)
        $redistributable = [Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName)
        $existingVersion = [version]("{0}.{1}.{2}.{3}" -f $existing.FileMajorPart, $existing.FileMinorPart, $existing.FileBuildPart, $existing.FilePrivatePart)
        $redistVersion = [version]("{0}.{1}.{2}.{3}" -f $redistributable.FileMajorPart, $redistributable.FileMinorPart, $redistributable.FileBuildPart, $redistributable.FilePrivatePart)
        if ($existingVersion -gt $redistVersion) {
            throw "The staged CRT is newer than the supplied VS redistributable: $($file.Name)"
        }
    }
    Copy-VerifiedFile $file.FullName $destination
}
if ($PackageKind -eq 'Application') {
    # The installer consumes this same list through a generated C++ header.
    $contract = Join-Path (Split-Path -Parent $PSScriptRoot) 'installer\native-required-files.txt'
    foreach ($required in (Get-Content -LiteralPath $contract)) {
        if ($required -notmatch '^[A-Za-z0-9_.-]+$' -or -not [IO.File]::Exists((Join-Path $output $required))) {
            throw "Required native application payload missing or invalid: $required"
        }
    }
}

$dependencies = New-Object 'System.Collections.Generic.List[object]'
$images = @(Get-ChildItem -LiteralPath $output -Recurse -File -Force | Where-Object { $_.Extension -in @('.exe', '.dll') })
foreach ($file in $images) {
    Assert-X64Image $file.FullName
    $inspection = @(& $dumpbinPath /NOLOGO /DEPENDENTS $file.FullName 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed for $($file.Name): $($inspection -join [Environment]::NewLine)" }
    foreach ($line in $inspection) {
        if ([string]$line -notmatch '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { continue }
        $name = $Matches[1]
        if ($name -match '^(api-ms-|ext-ms-)') {
            $resolution = 'windows-api-set'
        } else {
            $besideImage = Join-Path $file.DirectoryName $name
            $besideExecutable = Join-Path $output $name
            if ([IO.File]::Exists($besideImage)) { $resolution = 'package:' + (Get-RelativePackagePath $besideImage) }
            elseif ([IO.File]::Exists($besideExecutable)) { $resolution = 'package:' + (Get-RelativePackagePath $besideExecutable) }
            elseif ($name -match '^(msvcp140|vcruntime140|concrt140)') { throw "CRT import must be app-local: $name" }
            elseif ([IO.File]::Exists((Join-Path $env:SystemRoot ('System32\' + $name)))) { $resolution = 'windows-system32' }
            else { throw "Unresolved static import: $name required by $(Get-RelativePackagePath $file.FullName)" }
        }
        $dependencies.Add([ordered]@{ image = Get-RelativePackagePath $file.FullName; import = $name; resolution = $resolution })
    }
}
if ($dependencies.Count -eq 0) { throw 'dumpbin produced no DLL imports; runtime validation did not exercise the applications.' }

if ($PackageKind -eq 'Prototype') {
@'
w11widget native architecture prototype (not the 0.7 release)

Run w11widget_native_panel.exe. Its settings button opens the adjacent
w11widget_native_settings.exe. Keep this directory intact.

This isolated prototype does not install itself, register packages, enable
autostart, modify the existing Qt installation, or persist settings.
Widget changes in the settings window are temporary and do not control the panel.

Packaging verifies x64 images, copied bytes and static DLL imports. GUI startup,
dynamic loading, settings interactions and process exit require a separate test.

The app-local Microsoft VC runtime comes from the installed Visual Studio
VC/Redist directory, under that installation's redistribution terms:
https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files
'@ | Set-Content -LiteralPath (Join-Path $output 'PROTOTYPE.txt') -Encoding UTF8
} else {
    @"
w11widget $Version - Windows 11 x64

Run w11widget.exe. Its settings command opens the adjacent
w11widget_native_settings.exe. Keep all runtime files in this directory.

The portable package does not register packages or enable automatic startup.
The installer creates a separate version directory and retains previous copies.
To roll back, close the current application and run the previous version's EXE.

The package manifest records file hashes and static DLL import validation.
GUI, settings persistence and process lifecycle are tested separately.
"@ | Set-Content -LiteralPath (Join-Path $output 'APPLICATION.txt') -Encoding UTF8
    @'
Third-party runtime notices

The licenses/ directory contains the unmodified root license and notice files
from the exact NuGet package versions pinned in native/settings/packages.config:
Windows App SDK Base, Foundation, InteractiveExperiences and WinUI, C++/WinRT,
and WebView2. Their source package names and file hashes are recorded in
package-manifest.json. Inclusion of a notice does not imply that a browser
runtime is bundled or used by this application.

The app-local Microsoft Visual C++ runtime DLLs come only from the installed
Visual Studio 2022 VC/Redist/MSVC directory. Redistribution is governed by that
Visual Studio installation's license terms and the Microsoft redistribution list:
https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files
https://learn.microsoft.com/visualstudio/releases/2022/redistribution

Windows OS components, including ICU, are supplied by Windows rather than this
package. This native package does not include Qt runtime files.
'@ | Set-Content -LiteralPath (Join-Path $output 'THIRD-PARTY-NOTICES.txt') -Encoding UTF8
}

$files = @(Get-ChildItem -LiteralPath $output -Recurse -File -Force | Where-Object { $_.FullName -ne $incomplete } | Sort-Object FullName | ForEach-Object {
    # Explicit properties also work with Measure-Object in Windows PowerShell 5.1.
    [pscustomobject][ordered]@{ path = Get-RelativePackagePath $_.FullName; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$manifest = [ordered]@{
    schemaVersion = 1
    kind = $(if ($PackageKind -eq 'Application') { 'native-application' } else { 'isolated-native-prototype' })
    version = $Version
    entryPoint = $entryPoint
    createdAtUtc = [DateTime]::UtcNow.ToString('o')
    sourceRevision = $SourceRevision
    architecture = 'x64'
    platform = [Environment]::OSVersion.VersionString
    validation = 'copied-byte-hashes-and-static-imports; runtime-not-tested-by-packager'
    excludedPdbCount = @($sourceItems | Where-Object { -not $_.PSIsContainer -and $_.Extension -ieq '.pdb' }).Count
    payloadFileCount = $files.Count
    payloadBytes = ($files | Measure-Object -Property bytes -Sum).Sum
    crtSource = $crt.Substring($crt.IndexOf('\VC\', [StringComparison]::OrdinalIgnoreCase) + 1)
    crtFiles = @($crtFiles.Name)
    licenseSources = @($licenseFiles | ForEach-Object { [pscustomobject]@{ package = $_.package; path = $_.path } })
    files = $files
    staticImports = @($dependencies.ToArray())
}
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $output 'package-manifest.json') -Encoding UTF8
$hashes = @($files | ForEach-Object { $_.sha256 + '  ' + $_.path })
$hashes += (Get-FileHash -LiteralPath (Join-Path $output 'package-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() + '  package-manifest.json'
$hashes | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS') -Encoding ASCII
Remove-Item -LiteralPath $incomplete
[pscustomobject]@{ directory = $output; payloadFiles = $manifest.payloadFileCount; payloadBytes = $manifest.payloadBytes; excludedPdbFiles = $manifest.excludedPdbCount; importedLibraries = $dependencies.Count; runtimeTested = $false }
