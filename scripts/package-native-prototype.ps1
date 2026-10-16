param(
    [Parameter(Mandatory = $true)][string]$PanelExecutable,
    [Parameter(Mandatory = $true)][string]$SettingsDirectory,
    [Parameter(Mandatory = $true)][string]$CrtDirectory,
    [Parameter(Mandatory = $true)][string]$Dumpbin,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$SourceRevision
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'package-native-runtime.ps1') @PSBoundParameters -PackageKind Prototype
