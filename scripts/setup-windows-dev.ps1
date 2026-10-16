param([string]$ToolRoot = (Join-Path $env:LOCALAPPDATA 'w11widget-dev'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Keep development tools out of the system PATH and the application install.
$venv = Join-Path $ToolRoot 'python'
$python = Join-Path $venv 'Scripts\python.exe'
New-Item -ItemType Directory -Force -Path $ToolRoot | Out-Null
if (-not (Test-Path $python)) {
    & python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Python virtual environment creation failed.' }
}
& $python -m pip install --disable-pip-version-check 'aqtinstall==3.3.0'
if ($LASTEXITCODE -ne 0) { throw 'Qt download tool installation failed.' }
$qt = Join-Path $ToolRoot 'Qt'
if (-not (Test-Path (Join-Path $qt '6.8.3\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake'))) {
    & $python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O $qt --archives qtbase qtdeclarative qtsvg qttools
    if ($LASTEXITCODE -ne 0) { throw 'Qt download or extraction failed.' }
}
Write-Output "Qt comparison runtime and headers prepared under $qt"
Write-Output 'No system PATH, startup entry or installed application was changed.'
