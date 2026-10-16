param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [Parameter(Mandatory = $true)]
    [string]$CabinetFile,
    [Parameter(Mandatory = $true)]
    [string]$DefinitionFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PackageDirectory = [System.IO.Path]::GetFullPath($PackageDirectory)
$CabinetFile = [System.IO.Path]::GetFullPath($CabinetFile)
$DefinitionFile = [System.IO.Path]::GetFullPath($DefinitionFile)

if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory 'w11widget.exe'))) {
    throw "Standalone package is missing w11widget.exe: $PackageDirectory"
}

$cabinetDirectory = Split-Path -Parent $CabinetFile
New-Item -ItemType Directory -Force -Path $cabinetDirectory | Out-Null
Remove-Item -LiteralPath $CabinetFile -Force -ErrorAction SilentlyContinue

$lines = @(
    ('.Set CabinetNameTemplate=' + [System.IO.Path]::GetFileName($CabinetFile))
    ('.Set DiskDirectoryTemplate=' + $cabinetDirectory)
    '.Set CompressionType=MSZIP'
    '.Set Cabinet=on'
    '.Set Compress=on'
    '.Set MaxDiskSize=0'
)

Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File -Force |
    Sort-Object FullName |
    Group-Object DirectoryName |
    ForEach-Object {
        $relativeDirectory = $_.Name.Substring($PackageDirectory.Length).TrimStart('\')
        $destination = $relativeDirectory
        $lines += '.Set DestinationDir=' + $destination
        foreach ($file in $_.Group) {
            $lines += '"' + $file.FullName + '" "' + $file.Name + '"'
        }
    }

[System.IO.File]::WriteAllLines($DefinitionFile, [string[]]$lines, [System.Text.Encoding]::ASCII)
$cabinetLog = $DefinitionFile + '.log'
& "$env:SystemRoot\System32\makecab.exe" /F $DefinitionFile | Out-File -LiteralPath $cabinetLog
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $CabinetFile)) {
    throw "makecab failed with exit code $LASTEXITCODE. See $cabinetLog"
}
Write-Host "CAB created: $CabinetFile ($((Get-Item -LiteralPath $CabinetFile).Length) bytes). Details: $cabinetLog"
