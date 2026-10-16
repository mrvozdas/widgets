function Import-NativeToolchain([string]$VisualStudioDirectory) {
    $vs = (Resolve-Path -LiteralPath $VisualStudioDirectory).ProviderPath.TrimEnd('\')
    $developerCommand = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $developerCommand -PathType Leaf)) { throw 'Visual Studio developer environment is missing.' }
    # Import only into this PowerShell process; never change machine/user environment variables.
    $environment = @(& $env:ComSpec /d /s /c ('""' + $developerCommand + '" -no_logo -arch=x64 -host_arch=x64 >nul && set"'))
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio developer environment setup failed.' }
    foreach ($line in $environment) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
    $cmakeDirectory = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $tools = [ordered]@{
        CMake = Join-Path $cmakeDirectory 'cmake.exe'
        CTest = Join-Path $cmakeDirectory 'ctest.exe'
        Ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        MSBuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
        Dumpbin = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\dumpbin.exe'
        Crt = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
    }
    foreach ($tool in $tools.GetEnumerator()) {
        if (-not (Test-Path -LiteralPath $tool.Value)) { throw "Required native toolchain path missing: $($tool.Key)" }
    }
    return [pscustomobject]$tools
}

function Invoke-NativeBuildCommand([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Native build command failed ($LASTEXITCODE): $Executable" }
}
