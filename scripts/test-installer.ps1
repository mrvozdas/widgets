param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$WorkRoot,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$LiveFixture
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Start-CheckedProcess {
    param([string]$Executable, [string]$Arguments, [string]$LogPrefix, [switch]$CleanQt)
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $Executable
    $start.Arguments = $Arguments
    $start.WorkingDirectory = Split-Path -Parent $Executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ($CleanQt) {
        foreach ($name in @($start.EnvironmentVariables.Keys)) {
            if ($name -match '^(QT_|QML|QSG_)') { $start.EnvironmentVariables.Remove($name) }
        }
        $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
        $start.EnvironmentVariables['QT_QPA_PLATFORM'] = 'windows'
        $start.EnvironmentVariables['QT_QUICK_BACKEND'] = 'software'
        $start.EnvironmentVariables['QML_DISABLE_DISK_CACHE'] = '1'
    }
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Could not start $Executable" }
        return [pscustomobject]@{
            Process = $process
            OutputTask = $process.StandardOutput.ReadToEndAsync()
            ErrorTask = $process.StandardError.ReadToEndAsync()
            LogPrefix = $LogPrefix
            Executable = $Executable
        }
    } catch { $process.Dispose(); throw }
}

function Stop-OwnedChild {
    param($Child)
    if ($null -eq $Child) { return }
    try {
        if (-not $Child.Process.HasExited) {
            # Only this exact child created by this test is eligible for cleanup.
            $Child.Process.Kill()
            $Child.Process.WaitForExit()
        }
    } catch { } finally { $Child.Process.Dispose() }
}

function Complete-CheckedProcess {
    param($Child)
    try {
        if (-not $Child.Process.WaitForExit(60000)) {
            throw "Verification child timed out: $($Child.Executable)"
        }
        $stdout = $Child.OutputTask.GetAwaiter().GetResult()
        $stderr = $Child.ErrorTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($Child.LogPrefix + '.stdout.log', $stdout)
        [IO.File]::WriteAllText($Child.LogPrefix + '.stderr.log', $stderr)
        return [pscustomobject]@{ ExitCode = $Child.Process.ExitCode; Output = $stdout; Error = $stderr }
    } finally { Stop-OwnedChild $Child }
}

function Invoke-CheckedProcess {
    param([string]$Executable, [string]$Arguments, [string]$LogPrefix, [switch]$CleanQt)
    $child = Start-CheckedProcess $Executable $Arguments $LogPrefix -CleanQt:$CleanQt
    return Complete-CheckedProcess $child
}

function Get-TreeHashes {
    param([string]$Directory)
    $hashes = @{}
    foreach ($file in (Get-ChildItem -LiteralPath $Directory -Recurse -Force -File)) {
        $relative = $file.FullName.Substring($Directory.Length).TrimStart('\')
        $hashes[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    return $hashes
}

function Assert-TreeHashes {
    param([string]$Directory, [hashtable]$Expected, [string]$Description)
    $actual = Get-TreeHashes $Directory
    if ($Expected.Count -ne $actual.Count) {
        throw "$Description file count changed: expected $($Expected.Count), got $($actual.Count)."
    }
    foreach ($relative in $Expected.Keys) {
        if (-not $actual.ContainsKey($relative) -or $actual[$relative] -ne $Expected[$relative]) {
            throw "$Description file missing or changed: $relative"
        }
    }
}

function Get-InstallDirectories {
    $applicationRoot = Join-Path $profileDirectory 'app'
    if (Test-Path -LiteralPath $applicationRoot -PathType Container) {
        Get-ChildItem -LiteralPath $applicationRoot -Directory | Select-Object -ExpandProperty FullName
    }
}

function Get-NewInstallDirectories {
    param([string[]]$Before, [int]$ExpectedCount)
    $created = @(Get-InstallDirectories | Where-Object { $_ -notin $Before })
    if ($created.Count -ne $ExpectedCount) {
        throw "Expected $ExpectedCount fresh installation directories, got $($created.Count)."
    }
    foreach ($directory in $created) {
        if (-not ([IO.Path]::GetFileName($directory).StartsWith($Version + '-', [StringComparison]::Ordinal))) {
            throw "Fresh installation directory lacks its version prefix: $directory"
        }
    }
    return $created
}

function Read-SetupLog {
    param([string]$Target)
    $path = Join-Path (Split-Path -Parent $Target) ([IO.Path]::GetFileName($Target) + '-setup.log')
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Setup log missing: $path" }
    return [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
}

function Assert-ReadyLog {
    param([string]$Target, [string]$Launch)
    $detail = Read-SetupLog $Target
    foreach ($required in @(
        ('Hedef: ' + $Target)
        ('Application: ' + (Join-Path $Target 'w11widget.exe'))
        'Status: ready'
        ('Launch: ' + $Launch)
    )) {
        if (-not $detail.Contains($required)) { throw "Setup log lacks '$required': $Target" }
    }
    return $detail
}

function Invoke-FreshInstall {
    param([string]$Label, [switch]$AllowLaunch)
    $before = @(Get-InstallDirectories)
    $arguments = '--install-root "' + $profileDirectory + '" --quiet'
    if (-not $AllowLaunch) { $arguments += ' --no-launch' }
    $result = Invoke-CheckedProcess $Installer $arguments (Join-Path $runDirectory $Label)
    if ($result.ExitCode -ne 0) { throw "$Label failed ($($result.ExitCode)); see $runDirectory" }
    $created = @(Get-NewInstallDirectories $before 1)
    [void]$targets.Add($created[0])
    return $created[0]
}

function Stop-LiveFixture {
    param([System.Diagnostics.Process]$Process, [Threading.EventWaitHandle]$Signal, [switch]$RequireSuccess)
    try {
        [void]$Signal.Set()
        if (-not $Process.WaitForExit(5000)) {
            # The helper is the exact workspace child started by this test.
            $Process.Kill()
            $Process.WaitForExit()
            if ($RequireSuccess) { throw 'Owned live fixture did not stop when its event was signaled.' }
        }
        if ($RequireSuccess -and $Process.ExitCode -ne 0) {
            throw "Live fixture exited with $($Process.ExitCode), so its lock was not maintained."
        }
    } finally { $Process.Dispose() }
}

$Installer = [IO.Path]::GetFullPath($Installer)
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\')
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
$LiveFixture = [IO.Path]::GetFullPath($LiveFixture)
$lockLibraryName = if (Test-Path -LiteralPath (Join-Path $PackageDirectory 'double-conversion.dll')) {
    'double-conversion.dll'
} else {
    'Qt6Core.dll'
}
foreach ($file in @($Installer, $LiveFixture, (Join-Path $PackageDirectory 'w11widget.exe'),
                     (Join-Path $PackageDirectory $lockLibraryName))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required fixture file missing: $file" }
}
$expected = Get-TreeHashes $PackageDirectory
$runDirectory = Join-Path $WorkRoot ([Guid]::NewGuid().ToString('N').Substring(0, 8))
$profileDirectory = Join-Path $runDirectory ('kullan' + [char]0x0131 + 'c' + [char]0x0131 + ' deneme')
$oldDirectory = Join-Path $profileDirectory 'old'
New-Item -ItemType Directory -Path $oldDirectory -Force | Out-Null
Get-ChildItem -LiteralPath $PackageDirectory -Force | Copy-Item -Destination $oldDirectory -Recurse
$oldExecutable = Join-Path $oldDirectory 'w11widget.exe'
Copy-Item -LiteralPath $LiveFixture -Destination $oldExecutable -Force
$oldLibrary = Join-Path $oldDirectory $lockLibraryName
$settings = Join-Path $profileDirectory 'settings.json'
[IO.File]::WriteAllText($settings, '{"existing_preferences":"preserve"}')
$settingsHash = (Get-FileHash -LiteralPath $settings -Algorithm SHA256).Hash
$targets = New-Object 'System.Collections.Generic.List[string]'
$stopEvent = $null
$fixture = $null
$firstLock = $null

try {
    $eventName = 'Local\w11widget-installer-test-' + [Guid]::NewGuid().ToString('N')
    $stopEvent = New-Object System.Threading.EventWaitHandle -ArgumentList @(
        $false, [Threading.EventResetMode]::ManualReset, $eventName)
    $fixtureStart = New-Object System.Diagnostics.ProcessStartInfo
    $fixtureStart.FileName = $oldExecutable
    $fixtureStart.Arguments = '--stop-event "' + $eventName + '" --hold "' + $oldLibrary + '"'
    $fixtureStart.WorkingDirectory = $oldDirectory
    $fixtureStart.UseShellExecute = $false
    $fixtureStart.CreateNoWindow = $true
    $fixture = New-Object System.Diagnostics.Process
    $fixture.StartInfo = $fixtureStart
    if (-not $fixture.Start()) { throw 'Could not start the owned live fixture.' }
    $ready = Join-Path $oldDirectory 'ready.txt'
    $wait = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path -LiteralPath $ready -PathType Leaf)) {
        if ($fixture.HasExited) { throw "Live fixture exited before ready: $($fixture.ExitCode)" }
        if ($wait.ElapsedMilliseconds -gt 5000) { throw 'Live fixture did not report readiness.' }
        Start-Sleep -Milliseconds 25
    }
    if ([IO.File]::ReadAllText($ready) -notmatch ('READY PID ' + $fixture.Id + '\b')) {
        throw 'Live fixture readiness did not identify the exact child PID.'
    }
    $oldHashes = Get-TreeHashes $oldDirectory

    $first = Invoke-FreshInstall 'first-install'
    [void](Assert-ReadyLog $first 'skipped-no-launch')
    $firstHashes = Get-TreeHashes $first
    $firstLock = [IO.File]::Open((Join-Path $first $lockLibraryName),
        [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)

    # Omit --no-launch intentionally: only the verified workspace helper should
    # suppress startup. No actual Qt application is launched during this check.
    $runningTarget = Invoke-FreshInstall 'running-install' -AllowLaunch
    $runningLog = Assert-ReadyLog $runningTarget 'skipped-running'
    $runningLine = @($runningLog -split '\r?\n' | Where-Object {
        $_ -match ('^Running: PID ' + $fixture.Id + ' ')
    })
    if ($runningLine.Count -ne 1 -or -not $runningLine[0].EndsWith($oldExecutable, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Already-running detection did not identify the exact owned fixture path/PID.'
    }

    foreach ($label in @('repeat-1', 'repeat-2')) {
        $target = Invoke-FreshInstall $label
        [void](Assert-ReadyLog $target 'skipped-no-launch')
    }

    $beforeConcurrent = @(Get-InstallDirectories)
    $arguments = '--install-root "' + $profileDirectory + '" --quiet --no-launch'
    $childOne = $null
    $childTwo = $null
    try {
        $childOne = Start-CheckedProcess $Installer $arguments (Join-Path $runDirectory 'concurrent-1')
        $childTwo = Start-CheckedProcess $Installer $arguments (Join-Path $runDirectory 'concurrent-2')
        $resultOne = Complete-CheckedProcess $childOne
        $childOne = $null
        $resultTwo = Complete-CheckedProcess $childTwo
        $childTwo = $null
        if ($resultOne.ExitCode -ne 0 -or $resultTwo.ExitCode -ne 0) {
            throw "Concurrent installs failed: $($resultOne.ExitCode), $($resultTwo.ExitCode)."
        }
    } finally {
        Stop-OwnedChild $childOne
        Stop-OwnedChild $childTwo
    }
    foreach ($target in @(Get-NewInstallDirectories $beforeConcurrent 2)) {
        [void]$targets.Add($target)
        [void](Assert-ReadyLog $target 'skipped-no-launch')
    }
    if ($fixture.HasExited) { throw 'Live fixture stopped before repeated/concurrent installation finished.' }
    # End the helper's bounded lifetime before bulk hashing/smoke work. The first
    # installed copy remains independently locked by this PowerShell process.
    $stoppingFixture = $fixture
    $fixture = $null
    Stop-LiveFixture $stoppingFixture $stopEvent -RequireSuccess
    $stopEvent.Dispose()
    $stopEvent = $null

    Assert-TreeHashes $oldDirectory $oldHashes 'Previous live installation'
    Assert-TreeHashes $first $firstHashes 'Locked first installation'
    if ((Get-FileHash -LiteralPath $settings -Algorithm SHA256).Hash -ne $settingsHash) {
        throw 'Repeated installation changed the existing settings sentinel.'
    }
    if (@($targets | Sort-Object -Unique).Count -ne 6) { throw 'An installation reused an existing directory.' }
    foreach ($target in $targets) { Assert-TreeHashes $target $expected 'Fresh installation' }
    Write-Host "PASS: six same-version installations use unique directories; all $($expected.Count) file hashes match in each."
    Write-Host "PASS: live old installation, locked first-copy $lockLibraryName and existing settings remain unchanged."
    Write-Host 'PASS: two concurrent installations preserve both prior copies and create separate complete directories.'
    Write-Host 'PASS: an already-running verified workspace helper suppresses automatic launch without being stopped.'

    # Explicit in-place diagnostics retain the negative test. --extract-to must
    # imply no-launch, and the error must identify the DLL from the user's report.
    $inPlaceArguments = '--extract-to "' + $first + '" --quiet'
    $result = Invoke-CheckedProcess $Installer $inPlaceArguments (Join-Path $runDirectory 'locked-in-place')
    $detail = Read-SetupLog $first
    [IO.File]::WriteAllText((Join-Path $runDirectory 'locked-extraction.log'), $detail)
    if ($result.ExitCode -eq 0 -or $detail -notmatch 'Windows 32:' -or $detail -notmatch ([regex]::Escape($lockLibraryName))) {
        throw "Explicit in-place extraction did not report $lockLibraryName and Windows 32."
    }
    $firstLock.Dispose()
    $firstLock = $null
    $result = Invoke-CheckedProcess $Installer $inPlaceArguments (Join-Path $runDirectory 'in-place-repair')
    if ($result.ExitCode -ne 0) { throw "Explicit repair after unlocking failed: $($result.ExitCode)." }
    [void](Assert-ReadyLog $first 'skipped-no-launch')
    Assert-TreeHashes $first $expected 'Explicit unlocked repair'
    Write-Host "PASS: explicit in-place extraction reports locked $lockLibraryName/Windows 32, then repairs after unlock."

    $beforeInvalid = @(Get-InstallDirectories)
    $invalidArguments = '--install-root "' + $profileDirectory + '" --extract-to "' + $first + '" --quiet'
    $result = Invoke-CheckedProcess $Installer $invalidArguments (Join-Path $runDirectory 'invalid-options')
    if ($result.ExitCode -eq 0 -or @(Get-InstallDirectories).Count -ne $beforeInvalid.Count) {
        throw 'Mutually exclusive installation options were accepted or created another directory.'
    }
    foreach ($target in $targets) {
        $label = 'deployed-smoke-' + [IO.Path]::GetFileName($target)
        $result = Invoke-CheckedProcess (Join-Path $target 'w11widget.exe') '--smoke-test' `
            (Join-Path $runDirectory $label) -CleanQt
        if ($result.ExitCode -ne 0 -or $result.Output -notmatch 'PASS \(all built-in modules loaded') {
            throw "Deployed smoke failed ($($result.ExitCode)): $($result.Error) See $runDirectory"
        }
    }
    Write-Host 'PASS: all built-in QML modules load in every fresh copy with development Qt paths removed; panels stay hidden.'
    $summary = @(
        "PASS: six fresh same-version copies each match $($expected.Count) package SHA256 hashes."
        'PASS: two concurrent installers create different complete directories.'
        'PASS: locked old/first-version files and existing settings remain unchanged.'
        'PASS: a verified live helper causes skipped-running; no user application is stopped.'
        "PASS: explicit in-place $lockLibraryName lock reports Windows 32; unlocked repair succeeds."
        'PASS: mutually exclusive --install-root/--extract-to options are rejected.'
        'PASS: all built-in modules load in every new copy with a clean Qt environment; panels stay hidden.'
    )
    [IO.File]::WriteAllLines((Join-Path $runDirectory 'verification.txt'), [string[]]$summary)
    Write-Host "INSTALLER VERIFICATION: PASS ($runDirectory)"
} finally {
    if ($null -ne $firstLock) { $firstLock.Dispose() }
    if ($null -ne $fixture) { Stop-LiveFixture $fixture $stopEvent }
    if ($null -ne $stopEvent) { $stopEvent.Dispose() }
}
