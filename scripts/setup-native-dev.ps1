param([string]$ToolRoot = (Join-Path $env:LOCALAPPDATA 'w11widget-dev'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
New-Item -ItemType Directory -Force -Path $ToolRoot | Out-Null
$bootstrapper = Join-Path $ToolRoot 'vs_BuildTools.exe'
Invoke-WebRequest 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $bootstrapper
$signature = Get-AuthenticodeSignature $bootstrapper
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'The Visual Studio bootstrapper does not have a valid Microsoft signature.'
}
$arguments = @('--quiet', '--wait', '--norestart', '--nocache',
    '--installPath', 'C:\BuildTools2022',
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    # C++/WinUI uses the Windows Store MSBuild toolset even when unpackaged.
    '--add', 'Microsoft.VisualStudio.ComponentGroup.UWP.VC.BuildTools',
    '--add', 'Microsoft.VisualStudio.ComponentGroup.WindowsAppDevelopment.VC.BuildTools',
    # The VC groups omit the x64 UWP MSBuild targets; the common group owns them.
    '--add', 'Microsoft.VisualStudio.ComponentGroup.UWP.BuildTools',
    '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.26100',
    '--add', 'Microsoft.VisualStudio.Component.VC.CMake.Project')
if (Test-Path 'C:\BuildTools2022\Common7\Tools\VsDevCmd.bat') {
    $arguments = @('modify') + $arguments
}
$process = Start-Process $bootstrapper -ArgumentList $arguments -Wait -PassThru
if ($process.ExitCode -notin @(0, 3010)) {
    throw "Visual Studio setup failed: exit $($process.ExitCode). See %TEMP%\dd_setup*.log."
}
Write-Output "Visual Studio setup exit: $($process.ExitCode)"
if ($process.ExitCode -eq 3010) { Write-Output 'A restart is required; no restart was performed.' }
& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -all -products '*' -format json
