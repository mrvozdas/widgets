#requires -Version 5.1
param(
    [Parameter(Mandatory = $true)][string]$PanelExecutable,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [switch]$CaptureWindows,
    [switch]$ExerciseSettingsUi,
    [switch]$Production
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or $PSVersionTable.PSEdition -ne 'Desktop') {
    throw 'Run with Windows PowerShell: powershell.exe -NoProfile -MTA -File test_settings_lifecycle.ps1 ...'
}
if ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
    throw 'This GUI check requires the interactive desktop session; session zero cannot verify window behavior.'
}
# UIA clients must use a non-UI MTA thread; STA can stall cross-process provider calls.
# https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading
if ([Threading.Thread]::CurrentThread.GetApartmentState() -ne [Threading.ApartmentState]::MTA) {
    throw 'Restart Windows PowerShell with -MTA for this UI Automation check.'
}

$panelPath = (Get-Item -LiteralPath $PanelExecutable).FullName
if ([IO.Path]::GetFileName($panelPath) -ne 'w11widget_native_panel.exe' -and
    -not ($Production -and [IO.Path]::GetFileName($panelPath) -eq 'w11widget.exe')) {
    throw 'Select the native prototype panel executable, not the installed application.'
}
$settingsPath = Join-Path ([IO.Path]::GetDirectoryName($panelPath)) 'w11widget_native_settings.exe'
if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
    throw 'Place the native panel beside the complete self-contained WinUI settings output before this check.'
}
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
if (-not ('NativeLifecycleWindows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class NativeLifecycleWindows {
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    private delegate bool EnumWindowProc(IntPtr window, IntPtr context);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool EnumWindows(EnumWindowProc callback, IntPtr context);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr window, StringBuilder text, int capacity);
    public static string Caption(IntPtr window) { StringBuilder text = new StringBuilder(2048); GetWindowTextW(window,text,text.Capacity); return text.ToString(); }
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect bounds);
    [DllImport("user32.dll")] private static extern IntPtr GetDlgItem(IntPtr window, int controlId);
    [DllImport("user32.dll")] private static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window, StringBuilder name, int capacity);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool PostMessageW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)] private struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll")] private static extern bool GetMonitorInfoW(IntPtr monitor, ref MonitorInfo info);
    public static IntPtr FindClassWindow(int processId, string type) {
        IntPtr selected = IntPtr.Zero;
        EnumWindowProc find = delegate(IntPtr window, IntPtr unused) {
            uint owner; GetWindowThreadProcessId(window, out owner);
            StringBuilder name = new StringBuilder(256); GetClassNameW(window, name, name.Capacity);
            if (owner == processId && name.ToString() == type) { selected = window; return false; }
            return true;
        };
        EnumWindows(find, IntPtr.Zero); return selected;
    }
    public static IntPtr Control(IntPtr parent, int id) { return GetDlgItem(parent, id); }
    public static bool Visible(IntPtr window) { return IsWindowVisible(window); }
    public static void Post(IntPtr window, uint message, int value) {
        if (!PostMessageW(window, message, new IntPtr(value), IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public static bool InsideWorkArea(IntPtr window) {
        Rect bounds; MonitorInfo monitor = new MonitorInfo(); monitor.Size = Marshal.SizeOf(monitor);
        if (!GetWindowRect(window, out bounds) || !GetMonitorInfoW(MonitorFromWindow(window, 2), ref monitor)) return false;
        return bounds.Left >= monitor.Work.Left && bounds.Top >= monitor.Work.Top && bounds.Right <= monitor.Work.Right && bounds.Bottom <= monitor.Work.Bottom;
    }

    public static IntPtr FindVisibleWindow(int processId) {
        IntPtr selected = IntPtr.Zero;
        long largestArea = 0;
        EnumWindowProc find = delegate(IntPtr window, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner != processId || !IsWindowVisible(window) || IsIconic(window)) return true;
            int cloaked;
            if (DwmGetWindowAttribute(window, 14, out cloaked, sizeof(int)) == 0 && cloaked != 0) return true;
            Rect bounds;
            if (!GetWindowRect(window, out bounds)) return true;
            long width = (long)bounds.Right - bounds.Left;
            long height = (long)bounds.Bottom - bounds.Top;
            if (width > 0 && height > 0 && width * height > largestArea) {
                selected = window;
                largestArea = width * height;
            }
            return true;
        };
        if (!EnumWindows(find, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
        return selected;
    }

    public static IntPtr FindSettingsButton(int processId) {
        // An owned flyout can be larger than the panel; identify the production class first.
        IntPtr parent = FindClassWindow(processId, "w11widget.NativePanel");
        if (parent == IntPtr.Zero) parent = FindVisibleWindow(processId);
        if (parent == IntPtr.Zero) return IntPtr.Zero;
        // ID 100 is the native prototype's SettingsButton constant in panel/main.cpp.
        IntPtr button = GetDlgItem(parent, 100);
        if (button == IntPtr.Zero) return IntPtr.Zero;
        uint owner;
        GetWindowThreadProcessId(button, out owner);
        StringBuilder name = new StringBuilder(64);
        if (owner != processId || GetDlgCtrlID(button) != 100 ||
            GetClassNameW(button, name, name.Capacity) == 0 ||
            !String.Equals(name.ToString(), "Button", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The prototype settings control identity did not match PID, ID 100 and Button class.");
        return button;
    }

    public static void RequestClose(int processId) {
        IntPtr window = FindVisibleWindow(processId);
        if (window == IntPtr.Zero) throw new InvalidOperationException("The owned process has no visible window to close.");
        uint owner;
        GetWindowThreadProcessId(window, out owner);
        if (owner != processId) throw new InvalidOperationException("Window ownership changed before WM_CLOSE.");
        if (!PostMessageW(window, 0x0010, IntPtr.Zero, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
}
'@
}

$owned = New-Object 'System.Collections.Generic.List[System.Diagnostics.Process]'
$panel = $null
$profilePath = Join-Path $env:LOCALAPPDATA 'w11widget\settings.json'
$profileBefore = if (Test-Path -LiteralPath $profilePath) { (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash } else { $null }
$result = [ordered]@{
    status = 'running'
    stage = 'initialized'
    stageStartedAtUtc = [DateTime]::UtcNow.ToString('o')
    stageHistory = @()
    failedStage = $null
    startedAtUtc = [DateTime]::UtcNow.ToString('o')
    panelProcessId = $null
    firstSettingsProcessId = $null
    secondSettingsProcessId = $null
    windowHandles = @{}
    panelButton = $null
    uiaProvider = [ordered]@{ registered = $false; assembly = $null; location = $null }
    repeatedOpenReusedProcess = $false
    panelSurvivedSettingsClose = $false
    settingsRestarted = $false
    ui = [ordered]@{
        status = 'skipped'; pages = @(); labelEdited = $false; originalListRestored = $false
        navigationLabelCommitted = $false; reorderMovedUp = $false; reorderRestored = $false
        widgetAddInvoked = $false; widgetAddReturned = $false
        widgetRemoveInvoked = $false; widgetRemoveReturned = $false; widgetCounts = @(); duplicateLabelsVisible = $false
    }
    production = [ordered]@{ enabled = [bool]$Production; checks = @(); profile = $null }
    profileUnchanged = $null
    cleanupPendingProcessIds = @()
    cleanupErrors = @()
    profileError = $null
    error = $null
    errorDetails = $null
}

function Save-LifecycleResult {
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'settings-lifecycle.json') -Encoding UTF8
}

function Set-LifecycleStage([string]$Stage) {
    $result.stage = $Stage
    $result.stageStartedAtUtc = [DateTime]::UtcNow.ToString('o')
    $entry = [pscustomobject]@{ stage = $Stage; atUtc = $result.stageStartedAtUtc }
    $result.stageHistory = @(@($result.stageHistory) + $entry | Select-Object -Last 64)
    Save-LifecycleResult
}

function Add-CleanupError([string]$Message) {
    $result.cleanupErrors += $Message
    $result.status = 'failed'
    # The first failure is the diagnosis; a later cleanup failure must never replace it.
    if (-not $result.error) {
        $result.error = $Message
        $result.failedStage = $result.stage
    }
    Save-LifecycleResult
}

function Initialize-Win32UiaProvider {
    Set-LifecycleStage 'uia:register-win32-client-provider'
    if (-not ('NativeLifecycleUia' -as [type])) {
        # The framework Win32 proxy supplies Button/Invoke, rather than only the generic HWND Pane provider.
        # Use a compiled caller for registration: the framework's lazy proxy loader inspects reflected call frames.
        # https://learn.microsoft.com/en-us/dotnet/api/system.windows.automation.clientsettings.registerclientsideproviderassembly
        Add-Type -ReferencedAssemblies ([Windows.Automation.ClientSettings].Assembly.Location) -TypeDefinition @'
using System.Reflection;

public static class NativeLifecycleUia {
    public static Assembly RegisterWin32Providers() {
        AssemblyName client = typeof(System.Windows.Automation.ClientSettings).Assembly.GetName();
        AssemblyName name = new AssemblyName("UIAutomationClientsideProviders");
        name.Version = client.Version;
        name.CultureInfo = client.CultureInfo;
        name.SetPublicKeyToken(client.GetPublicKeyToken());
        Assembly providers = Assembly.Load(name);
        System.Windows.Automation.ClientSettings.RegisterClientSideProviderAssembly(providers.GetName());
        return providers;
    }
}
'@
    }
    $providers = [NativeLifecycleUia]::RegisterWin32Providers()
    $result.uiaProvider.registered = $true
    $result.uiaProvider.assembly = $providers.FullName
    $result.uiaProvider.location = $providers.Location
    Save-LifecycleResult
}

function Get-OwnedSettings {
    # Both parent PID and the exact executable path must match before a child is ever controlled.
    if ($null -eq $panel) { return }
    Get-CimInstance Win32_Process -Filter "ParentProcessId = $($panel.Id) AND Name = 'w11widget_native_settings.exe'" |
        Where-Object { $_.ExecutablePath -and [IO.Path]::GetFullPath($_.ExecutablePath) -eq $settingsPath }
}

function Track-OwnedSettings([int]$ChildId) {
    $process = [Diagnostics.Process]::GetProcessById($ChildId)
    try {
        $null = $process.Handle
        # Recheck ownership after retaining the process handle, closing the enumeration/PID-reuse gap.
        if ($process.HasExited -or @((Get-OwnedSettings) | Where-Object { [int]$_.ProcessId -eq $ChildId }).Count -ne 1 -or
            [IO.Path]::GetFullPath($process.MainModule.FileName) -ne $settingsPath) {
            throw 'The settings process identity changed before it could be retained.'
        }
        $owned.Add($process)
        return $process
    } catch { $process.Dispose(); throw }
}

function Wait-OwnedWindow([Diagnostics.Process]$Process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($Process.HasExited) { throw "Owned process $($Process.Id) exited before showing its window." }
        # WinUI can expose MainWindowHandle=0 despite a visible HWND; enumerate only to locate this retained PID.
        $handle = [NativeLifecycleWindows]::FindVisibleWindow($Process.Id)
        if ($handle -ne [IntPtr]::Zero) {
            $key = [string]$Process.Id
            $description = '0x' + $handle.ToInt64().ToString('x')
            if (-not $result.windowHandles.ContainsKey($key) -or $result.windowHandles[$key] -ne $description) {
                $result.windowHandles[$key] = $description
                Save-LifecycleResult
            }
            $window = [Windows.Automation.AutomationElement]::FromHandle($handle)
            if ($null -ne $window -and $window.Current.ProcessId -eq $Process.Id) { return $window }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "No visible UI Automation window for owned process $($Process.Id) within 15 seconds."
}

function Invoke-OwnedSettingsButton {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $invoke = $null
    $result.panelButton = [ordered]@{
        processId = $panel.Id; nativeControlId = 100; nativeButtonHandle = $null
        matchingButtonCount = 0; name = $null; controlType = $null; lastError = $null
        descendantCount = 0; descendants = @(); diagnosticError = $null
    }
    do {
        if ($panel.HasExited) { throw 'The owned panel exited before its settings button became available.' }
        try {
            $handle = [NativeLifecycleWindows]::FindSettingsButton($panel.Id)
            if ($handle -ne [IntPtr]::Zero) {
                $result.panelButton.nativeButtonHandle = '0x' + $handle.ToInt64().ToString('x')
                # Address the verified child HWND directly; a top-level UIA wrapper may not yet enumerate its children.
                $button = [Windows.Automation.AutomationElement]::FromHandle($handle)
                if ($null -ne $button -and $button.Current.ProcessId -eq $panel.Id) {
                    $result.panelButton.name = $button.Current.Name
                    $result.panelButton.controlType = $button.Current.ControlType.ProgrammaticName
                    if ($button.Current.Name -eq 'Ayarlar' -and $button.Current.ControlType -eq [Windows.Automation.ControlType]::Button) {
                        $pattern = $null
                        if ($button.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
                            $invoke = [Windows.Automation.InvokePattern]$pattern
                            $result.panelButton.matchingButtonCount = 1
                        }
                    }
                }
            }
        } catch { $result.panelButton.lastError = $_.Exception.Message }
        Save-LifecycleResult
        if ($null -ne $invoke) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($null -eq $invoke) {
        try {
            $handle = [NativeLifecycleWindows]::FindVisibleWindow($panel.Id)
            if ($handle -ne [IntPtr]::Zero) {
                $window = [Windows.Automation.AutomationElement]::FromHandle($handle)
                $children = $window.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)
                for ($index = 0; $index -lt $children.Count; ++$index) {
                    $current = $children[$index].Current
                    if ($current.ProcessId -ne $panel.Id) { continue }
                    $result.panelButton.descendantCount += 1
                    if ($result.panelButton.descendants.Count -lt 64) {
                        $result.panelButton.descendants += [pscustomobject]@{
                            name = $current.Name; controlType = $current.ControlType.ProgrammaticName
                            className = $current.ClassName; automationId = $current.AutomationId
                            nativeWindowHandle = $current.NativeWindowHandle
                        }
                    }
                }
            }
        } catch { $result.panelButton.diagnosticError = $_.Exception.Message }
        Save-LifecycleResult
        throw 'The owned Ayarlar button did not expose the expected UIA Invoke pattern within 15 seconds; see panelButton diagnostics.'
    }
    # Invoke once, outside the discovery retry loop, so an action error never causes a second activation.
    $invoke.Invoke()
}

function Wait-OwnedSettings {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($panel.HasExited) { throw 'The owned panel exited while opening settings.' }
        $children = @(Get-OwnedSettings)
        if ($children.Count -gt 1) { throw 'The owned panel created more than one settings process.' }
        if ($children.Count -eq 1) {
            return Track-OwnedSettings ([int]$children[0].ProcessId)
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The owned panel did not launch the exact sibling settings executable within 15 seconds.'
}

function Close-OwnedWindow([Diagnostics.Process]$Process) {
    $window = Wait-OwnedWindow $Process
    if ($window.Current.ProcessId -ne $Process.Id) { throw 'Window ownership changed before close.' }
    $pattern = [Windows.Automation.WindowPattern]$window.GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern)
    $pattern.Close()
    if (-not $Process.WaitForExit(10000)) { throw "Owned process $($Process.Id) remained alive after its window closed." }
    if ($Process.ExitCode -ne 0) { throw "Owned process $($Process.Id) exited with code $($Process.ExitCode)." }
}

function Read-SettingsLabels([string]$SourcePath = (Join-Path $PSScriptRoot '..\settings\strings\tr-TR.h')) {
    # Read the source localization table instead of duplicating Turkish strings in this test.
    $source = [IO.File]::ReadAllText($SourcePath, [Text.Encoding]::UTF8)
    $labels = @{}
    foreach ($match in [regex]::Matches($source, 'inline constexpr auto (\w+) = L"([^"\\]*)";')) {
        $labels[$match.Groups[1].Value] = $match.Groups[2].Value
    }
    foreach ($key in @('widgets', 'layout', 'appearance', 'addWidget', 'displayName', 'moveUp', 'moveDown', 'remove', 'monitor', 'align', 'theme', 'close', 'utc')) {
        if (-not $labels.ContainsKey($key)) { throw "Missing source localization key: $key" }
    }
    return $labels
}

function Find-OwnedControl([Diagnostics.Process]$Process, [string]$Name, [Windows.Automation.ControlType]$Type) {
    Set-LifecycleStage "ui:find:$($Type.ProgrammaticName):$Name"
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $condition = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, $Type))
    do {
        $window = Wait-OwnedWindow $Process
        $controls = $window.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        if ($controls.Count -gt 1) { throw "Ambiguous owned control: $Name ($($Type.ProgrammaticName))." }
        if ($controls.Count -eq 1 -and $controls[0].Current.ProcessId -eq $Process.Id) { return $controls[0] }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Missing owned control: $Name ($($Type.ProgrammaticName))."
}

function Select-OwnedSettingsPage([Diagnostics.Process]$Process, [string]$Name) {
    Set-LifecycleStage "ui:navigation:find:$Name"
    $window = Wait-OwnedWindow $Process
    $matches = $window.FindAll([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
    $actions = @()
    for ($index = 0; $index -lt $matches.Count; ++$index) {
        $element = $matches[$index]
        if ($element.Current.ProcessId -ne $Process.Id) { continue }
        $pattern = $null
        # The page heading/list can share its name with the menu item; only the selectable/invokable item is actionable.
        if ($element.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern, [ref]$pattern)) {
            $actions += [pscustomobject]@{ kind = 'select'; pattern = $pattern }
        } elseif ($element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
            $actions += [pscustomobject]@{ kind = 'invoke'; pattern = $pattern }
        }
    }
    if ($actions.Count -ne 1) { throw "Expected one actionable navigation item named $Name in the owned settings process." }
    Set-LifecycleStage "ui:navigation:$($actions[0].kind):$Name"
    if ($actions[0].kind -eq 'select') { ([Windows.Automation.SelectionItemPattern]$actions[0].pattern).Select() }
    else { ([Windows.Automation.InvokePattern]$actions[0].pattern).Invoke() }
}

function Read-OwnedWidgetNames([Diagnostics.Process]$Process, [string]$ListName, [int]$ExpectedCount) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $list = Find-OwnedControl $Process $ListName ([Windows.Automation.ControlType]::List)
        Set-LifecycleStage "ui:widgets:read-items:expected-$ExpectedCount"
        $items = $list.FindAll([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,
                [Windows.Automation.ControlType]::ListItem))
        $count = [pscustomobject]@{ actual = $items.Count; expected = $ExpectedCount; atUtc = [DateTime]::UtcNow.ToString('o') }
        $result.ui.widgetCounts = @(@($result.ui.widgetCounts) + $count | Select-Object -Last 16)
        Save-LifecycleResult
        if ($items.Count -eq $ExpectedCount) {
            for ($index = 0; $index -lt $items.Count; ++$index) {
                if ($items[$index].Current.ProcessId -ne $Process.Id) { throw 'Widget item ownership changed.' }
                $items[$index].Current.Name
            }
            return
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected $ExpectedCount widget items in the owned settings list."
}

function Assert-OwnedWidgetOrder([Diagnostics.Process]$Process, [string]$ListName, [string[]]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $actual = @(Read-OwnedWidgetNames $Process $ListName $Expected.Count)
        if (($actual -join [char]0) -ceq ($Expected -join [char]0)) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The owned widget list did not reach the expected order.'
}

function Exercise-OwnedSettings([Diagnostics.Process]$Process) {
    Set-LifecycleStage 'ui:read-localization'
    $labels = Read-SettingsLabels
    $result.ui.status = 'running'
    Select-OwnedSettingsPage $Process $labels.widgets
    $add = Find-OwnedControl $Process $labels.addWidget ([Windows.Automation.ControlType]::Button)
    $null = Find-OwnedControl $Process $labels.displayName ([Windows.Automation.ControlType]::Edit)
    $before = @(Read-OwnedWidgetNames $Process $labels.widgets 4)
    $result.ui.widgetAddInvoked = $true
    Set-LifecycleStage 'ui:widgets:add:invoke'
    ([Windows.Automation.InvokePattern]$add.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke()
    $result.ui.widgetAddReturned = $true
    Save-LifecycleResult
    $added = @(Read-OwnedWidgetNames $Process $labels.widgets 5)
    $originalUtcCount = @($before | Where-Object { $_ -ceq $labels.utc }).Count
    if (@($added | Where-Object { $_ -ceq $labels.utc }).Count -ne $originalUtcCount + 1) {
        throw 'The duplicate UTC label did not remain a separate accessible list item after adding a world clock.'
    }
    $result.ui.duplicateLabelsVisible = $true
    Save-LifecycleResult
    $editor = Find-OwnedControl $Process $labels.displayName ([Windows.Automation.ControlType]::Edit)
    Set-LifecycleStage 'ui:widgets:label:get-value-pattern'
    $value = [Windows.Automation.ValuePattern]$editor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
    $temporaryLabel = 'UIA prototype widget'
    Set-LifecycleStage 'ui:widgets:label:focus-editor'
    $editor.SetFocus()
    Set-LifecycleStage 'ui:widgets:label:set-value'
    $value.SetValue($temporaryLabel)
    # Navigation removes the focused inspector, exercising LostFocus during page teardown.
    Set-LifecycleStage 'ui:widgets:label:commit-navigation-blur'
    Select-OwnedSettingsPage $Process $labels.layout
    $null = Find-OwnedControl $Process $labels.monitor ([Windows.Automation.ControlType]::ComboBox)
    Select-OwnedSettingsPage $Process $labels.widgets
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $edited = @(Read-OwnedWidgetNames $Process $labels.widgets 5)
        if ($edited[4] -eq $temporaryLabel) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($edited[4] -ne $temporaryLabel) { throw 'The edited label was not committed to the selected widget list item.' }
    $returnedEditor = Find-OwnedControl $Process $labels.displayName ([Windows.Automation.ControlType]::Edit)
    $returnedValue = [Windows.Automation.ValuePattern]$returnedEditor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
    if ($returnedValue.Current.Value -cne $temporaryLabel) { throw 'Returning from navigation did not preserve the selected widget inspector and edited label.' }
    $result.ui.labelEdited = $true
    $result.ui.navigationLabelCommitted = $true
    Save-LifecycleResult

    $up = Find-OwnedControl $Process $labels.moveUp ([Windows.Automation.ControlType]::Button)
    if (-not $up.Current.IsEnabled) { throw 'Move up is disabled for the last widget.' }
    Set-LifecycleStage 'ui:widgets:reorder:up:invoke'
    ([Windows.Automation.InvokePattern]$up.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke()
    $expectedUp = @($edited)
    $expectedUp[3] = $edited[4]
    $expectedUp[4] = $edited[3]
    Assert-OwnedWidgetOrder $Process $labels.widgets $expectedUp
    $result.ui.reorderMovedUp = $true
    Save-LifecycleResult
    $down = Find-OwnedControl $Process $labels.moveDown ([Windows.Automation.ControlType]::Button)
    if (-not $down.Current.IsEnabled) { throw 'Move down is disabled after moving the last widget up.' }
    Set-LifecycleStage 'ui:widgets:reorder:down:invoke'
    ([Windows.Automation.InvokePattern]$down.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke()
    Assert-OwnedWidgetOrder $Process $labels.widgets $edited
    $result.ui.reorderRestored = $true
    Save-LifecycleResult

    $remove = Find-OwnedControl $Process $labels.remove ([Windows.Automation.ControlType]::Button)
    $result.ui.widgetRemoveInvoked = $true
    Set-LifecycleStage 'ui:widgets:remove:invoke'
    ([Windows.Automation.InvokePattern]$remove.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke()
    $result.ui.widgetRemoveReturned = $true
    Save-LifecycleResult
    $after = @(Read-OwnedWidgetNames $Process $labels.widgets 4)
    if (($before -join [char]0) -cne ($after -join [char]0)) { throw 'Adding/editing/removing one widget did not restore the original ordered list.' }
    $result.ui.originalListRestored = $true

    # UI exercise always records each page; lifecycle-only captures remain controlled by -CaptureWindows.
    foreach ($page in @('widgets', 'layout', 'appearance')) {
        Select-OwnedSettingsPage $Process $labels[$page]
        switch ($page) {
            'widgets' {
                $null = Find-OwnedControl $Process $labels.widgets ([Windows.Automation.ControlType]::List)
                $null = Find-OwnedControl $Process $labels.displayName ([Windows.Automation.ControlType]::Edit)
            }
            'layout' {
                $null = Find-OwnedControl $Process $labels.monitor ([Windows.Automation.ControlType]::ComboBox)
                $null = Find-OwnedControl $Process $labels.align ([Windows.Automation.ControlType]::ComboBox)
            }
            'appearance' {
                $null = Find-OwnedControl $Process $labels.theme ([Windows.Automation.ControlType]::ComboBox)
                $null = Find-OwnedControl $Process $labels.close ([Windows.Automation.ControlType]::Button)
            }
        }
        Set-LifecycleStage "ui:capture:$page"
        & "$PSScriptRoot\..\..\scripts\capture-window.ps1" -ProcessId $Process.Id -OutputPng (Join-Path $outputPath "settings-$page.png") | Out-Null
        $result.ui.pages += $page
    }
    $result.ui.status = 'passed'
}

function Invoke-NativeControl([IntPtr]$Parent, [int]$Id) {
    $handle = [NativeLifecycleWindows]::Control($Parent, $Id)
    if ($handle -eq [IntPtr]::Zero) { throw "Native control $Id is missing." }
    $element = [Windows.Automation.AutomationElement]::FromHandle($handle)
    if ($element.Current.ProcessId -ne $panel.Id -or $element.Current.ControlType -ne [Windows.Automation.ControlType]::Button -or -not $element.Current.Name) {
        throw "Native control $Id does not expose an owned, named UIA Button."
    }
    ([Windows.Automation.InvokePattern]$element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke()
}
function Wait-NativePopup([bool]$Present) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $handle = [NativeLifecycleWindows]::FindClassWindow($panel.Id, 'w11widget.WidgetPopup')
        if (($handle -ne [IntPtr]::Zero) -eq $Present) { return $handle }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Native popup presence did not become $Present."
}
function Native-Check([string]$Name) {
    $result.production.checks += $Name
    Save-LifecycleResult
}
function Exercise-NativePanel {
    $root = [NativeLifecycleWindows]::FindClassWindow($panel.Id, 'w11widget.NativePanel')
    if ($root -eq [IntPtr]::Zero) { throw 'Production panel HWND missing.' }
    foreach ($entry in @(@(201, 'world-clock'), @(200, 'countdown'))) {
        Set-LifecycleStage ('native:popup:' + $entry[1])
        Invoke-NativeControl $root $entry[0]
        $popup = Wait-NativePopup $true
        if (-not [NativeLifecycleWindows]::InsideWorkArea($popup)) { throw 'Widget popup exceeds its monitor work area.' }
        if ($CaptureWindows) { & "$PSScriptRoot\..\..\scripts\capture-window.ps1" -ProcessId $panel.Id -OutputPng (Join-Path $outputPath ('popup-' + $entry[1] + '.png')) | Out-Null }
        [NativeLifecycleWindows]::Post($popup, 0x0100, 27)
        $null = Wait-NativePopup $false
        Native-Check ($entry[1] + ':UIA-invoke,clamped,Escape')
    }
    Set-LifecycleStage 'native:focus:start-pause-reset'
    Invoke-NativeControl $root 203
    $popup = Wait-NativePopup $true
    Invoke-NativeControl $popup 302
    Start-Sleep -Milliseconds 1400
    Invoke-NativeControl $popup 302
    $focusHandle = [NativeLifecycleWindows]::Control($root, 203)
    $focusName = [Windows.Automation.AutomationElement]::FromHandle($focusHandle).Current.Name
    if ($focusName -notmatch '00:5[89]') { throw "Focus timer did not advance: $focusName" }
    Invoke-NativeControl $popup 303
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $focusName = [Windows.Automation.AutomationElement]::FromHandle($focusHandle).Current.Name
        if ($focusName -match '01:00') { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($focusName -notmatch '01:00') {
        if ($CaptureWindows) { & "$PSScriptRoot\..\..\scripts\capture-window.ps1" -ProcessId $panel.Id -OutputPng (Join-Path $outputPath 'focus-reset-failure.png') | Out-Null }
        throw "Focus reset failed: UIA=$focusName; HWND=$([NativeLifecycleWindows]::Caption($focusHandle)); popup=$([NativeLifecycleWindows]::Caption([NativeLifecycleWindows]::Control($popup,301)))"
    }
    if ($CaptureWindows) { & "$PSScriptRoot\..\..\scripts\capture-window.ps1" -ProcessId $panel.Id -OutputPng (Join-Path $outputPath 'popup-focus.png') | Out-Null }
    Invoke-NativeControl $popup 302
    [NativeLifecycleWindows]::Post($root, 0x0111, 402)
    Start-Sleep -Milliseconds 100
    if ([NativeLifecycleWindows]::Visible($root)) { throw 'Hide did not hide the panel.' }
    $null = Wait-NativePopup $false
    Start-Sleep -Milliseconds 1100
    [NativeLifecycleWindows]::Post($root, 0x0111, 401)
    Start-Sleep -Milliseconds 100
    Invoke-NativeControl $root 203
    $popup = Wait-NativePopup $true
    $action = [Windows.Automation.AutomationElement]::FromHandle([NativeLifecycleWindows]::Control($popup,302))
    if ($action.Current.Name -ne 'Sürdür') { throw 'Hidden focus timer resumed automatically.' }
    Invoke-NativeControl $popup 305
    Native-Check 'focus:start,pause,reset,hide-pauses,no-auto-resume'

    Set-LifecycleStage 'native:profile-watch'
    $fixture = Get-Content -LiteralPath $result.production.profile -Raw -Encoding UTF8 | ConvertFrom-Json
    $fixture.widgets[1].settings.label = 'UTC changed'
    $newProfile = $result.production.profile + '.new'
    [IO.File]::WriteAllText($newProfile, ($fixture | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
    [IO.File]::Replace($newProfile, $result.production.profile, ($result.production.profile + '.before-reload'))
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $clock = [NativeLifecycleWindows]::Control($root,201)
        # Reload replaces child HWNDs; wait until the replacement control exists.
        $label = if ($clock -ne [IntPtr]::Zero) { [NativeLifecycleWindows]::Caption($clock) } else { '' }
        if ($label -like 'UTC changed*') { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($label -notlike 'UTC changed*') { throw 'Atomic profile replacement did not update the panel.' }
    Native-Check 'atomic-file-change-reloads-panel'

    Set-LifecycleStage 'native:single-instance'
    $second = Start-Process -FilePath $panelPath -ArgumentList @('--settings-file', ('"' + $result.production.profile + '"')) -PassThru
    try { if (-not $second.WaitForExit(5000) -or $second.ExitCode -ne 0) { throw 'Second profile launch did not activate existing panel.' } }
    finally { $second.Dispose() }
    Native-Check 'same-profile-single-instance'
}

try {
    Initialize-Win32UiaProvider
    # Each production test owns a separate profile under its output directory.
    Set-LifecycleStage 'panel:start'
    $launchArguments = @('--benchmark', '--duration-seconds', '180')
    if ($Production) {
        $isolated = Join-Path $outputPath 'profile\settings.json'
        if (Test-Path -LiteralPath $isolated) { throw 'Production test output profile must be new.' }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($isolated)) | Out-Null
        $fixture = @{
            schema_version = 2
            panel = @{ monitor = 'primary'; side = 'left'; width_dip = 560; offset_dip = 150; opacity = 1.0; hide_for_fullscreen = $false; theme = 'dark' }
            widgets = @(
                @{ instance_id='countdown-1'; type_id='countdown'; enabled=$true; visible=$true; width_dip=150; settings=@{label='Geri sayım';target_time='18:00';time_zone='Europe/Istanbul';daily_repeat=$true;target_date=''} },
                @{ instance_id='clock-1'; type_id='world-clock'; enabled=$true; visible=$true; width_dip=100; settings=@{label='UTC';time_zone='UTC';show_date=$false} },
                @{ instance_id='clock-2'; type_id='world-clock'; enabled=$true; visible=$true; width_dip=100; settings=@{label='İstanbul';time_zone='Europe/Istanbul';show_date=$false} },
                @{ instance_id='focus-1'; type_id='focus-timer'; enabled=$true; visible=$true; width_dip=100; settings=@{label='Odak';duration_minutes=1} }
            )
        }
        [IO.File]::WriteAllText($isolated, ($fixture | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
        $result.production.profile = $isolated
        $launchArguments = @('--settings-file', ('"'+$isolated+'"'), '--duration-seconds', '180', '--metrics-file', ('"' + (Join-Path $outputPath 'panel-metrics.json') + '"'))
    }
    $panel = Start-Process -FilePath $panelPath -WorkingDirectory ([IO.Path]::GetDirectoryName($panelPath)) `
        -ArgumentList $launchArguments -PassThru `
        -RedirectStandardOutput (Join-Path $outputPath 'panel.stdout.log') `
        -RedirectStandardError (Join-Path $outputPath 'panel.stderr.log')
    $null = $panel.Handle
    $owned.Add($panel)
    $result.panelProcessId = $panel.Id
    Set-LifecycleStage 'panel:wait-window'
    $null = Wait-OwnedWindow $panel
    if ($Production) { Exercise-NativePanel }
    if (@(Get-OwnedSettings).Count -ne 0) { throw 'Settings started before the settings button was invoked.' }

    Set-LifecycleStage 'settings:first-open:invoke-panel-button'
    Invoke-OwnedSettingsButton
    Set-LifecycleStage 'settings:first-open:wait-process'
    $first = Wait-OwnedSettings
    $result.firstSettingsProcessId = $first.Id
    Set-LifecycleStage 'settings:first-open:wait-window'
    $null = Wait-OwnedWindow $first
    if ($ExerciseSettingsUi) { Exercise-OwnedSettings $first }
    if ($Production) {
        Set-LifecycleStage 'native:popup-edit-reuses-settings';
        $root = [NativeLifecycleWindows]::FindClassWindow($panel.Id, 'w11widget.NativePanel')
        Invoke-NativeControl $root 201
        $popup = Wait-NativePopup $true
        Invoke-NativeControl $popup 304
        $null = Wait-NativePopup $false
        $labels = Read-SettingsLabels
        $editor = Find-OwnedControl $first $labels.displayName ([Windows.Automation.ControlType]::Edit)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $name = ([Windows.Automation.ValuePattern]$editor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)).Current.Value
            if ($name -eq 'UTC changed') { break }
            Start-Sleep -Milliseconds 50
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($name -ne 'UTC changed') { throw 'Popup edit did not select its widget in the existing settings process.' }
        Native-Check 'popup-edit-selects-widget-in-existing-settings'
        Set-LifecycleStage 'native:popup-outside-activation'
        Invoke-NativeControl $root 201
        $null = Wait-NativePopup $true
        Invoke-OwnedSettingsButton
        $null = Wait-NativePopup $false
        Native-Check 'outside-activation-dismisses-popup'
    }
    if ($CaptureWindows) {
        Set-LifecycleStage 'settings:first-open:capture'
        & "$PSScriptRoot\..\..\scripts\capture-window.ps1" -ProcessId $first.Id -OutputPng (Join-Path $outputPath 'settings-open.png') | Out-Null
    }

    Set-LifecycleStage 'settings:repeat-open:invoke-panel-button'
    Invoke-OwnedSettingsButton
    Set-LifecycleStage 'settings:repeat-open:verify-same-process'
    Start-Sleep -Milliseconds 500
    $children = @(Get-OwnedSettings)
    if ($children.Count -ne 1 -or [int]$children[0].ProcessId -ne $first.Id) {
        throw 'Opening settings again did not reuse the same owned settings process.'
    }
    $result.repeatedOpenReusedProcess = $true
    Set-LifecycleStage 'settings:first-close:window-pattern-close'
    Close-OwnedWindow $first
    if ($panel.HasExited) { throw 'The panel exited when settings closed.' }
    Set-LifecycleStage 'panel:verify-alive-after-first-close'
    $null = Wait-OwnedWindow $panel
    $result.panelSurvivedSettingsClose = $true

    Set-LifecycleStage 'settings:second-open:invoke-panel-button'
    Invoke-OwnedSettingsButton
    Set-LifecycleStage 'settings:second-open:wait-process'
    $second = Wait-OwnedSettings
    $result.secondSettingsProcessId = $second.Id
    if ($second.Id -eq $first.Id) { throw 'Reopening settings did not create a new process.' }
    Set-LifecycleStage 'settings:second-open:wait-window'
    $null = Wait-OwnedWindow $second
    $result.settingsRestarted = $true
    Set-LifecycleStage 'settings:second-close:window-pattern-close'
    Close-OwnedWindow $second
    if ($panel.HasExited) { throw 'The panel exited after the second settings close.' }
    if ($CaptureWindows) {
        Set-LifecycleStage 'panel:capture-after-settings-close'
        & "$PSScriptRoot\..\..\scripts\capture-window.ps1" -ProcessId $panel.Id -OutputPng (Join-Path $outputPath 'panel-after-settings-close.png') | Out-Null
    }
    Set-LifecycleStage 'panel:close'
    if ($Production) {
        [NativeLifecycleWindows]::Post([NativeLifecycleWindows]::FindClassWindow($panel.Id, 'w11widget.NativePanel'), 0x0111, 403)
        if (-not $panel.WaitForExit(5000) -or $panel.ExitCode -ne 0) { throw 'Production panel did not exit normally.' }
    } else { Close-OwnedWindow $panel }
    $result.status = 'passed'
} catch {
    $result.status = 'failed'
    if ($result.ui.status -eq 'running') { $result.ui.status = 'failed' }
    $result.error = $_.Exception.Message
    $result.errorDetails = $_.ScriptStackTrace
    $result.failedStage = $result.stage
    Save-LifecycleResult
} finally {
    # Clean up only retained processes from this run; no process-name termination or unfiltered window mutation is used.
    try {
        Set-LifecycleStage 'cleanup:verify-child-ownership'
        foreach ($child in @(Get-OwnedSettings)) {
            if (@($owned | Where-Object { $_.Id -eq [int]$child.ProcessId }).Count -eq 0) {
                $null = Track-OwnedSettings ([int]$child.ProcessId)
            }
        }
    } catch { Add-CleanupError "Cleanup ownership check failed: $($_.Exception.Message)" }
    for ($index = $owned.Count - 1; $index -ge 0; --$index) {
        $process = $owned[$index]
        try {
            Set-LifecycleStage "cleanup:close-owned-process:$($process.Id)"
            if (-not $process.HasExited) {
                if ($Production -and $process.Id -eq $panel.Id) {
                    [NativeLifecycleWindows]::Post([NativeLifecycleWindows]::FindClassWindow($panel.Id, 'w11widget.NativePanel'), 0x0111, 403)
                } else { [NativeLifecycleWindows]::RequestClose($process.Id) }
                if (-not $process.WaitForExit(5000)) {
                    $result.cleanupPendingProcessIds += $process.Id
                    Add-CleanupError "Owned prototype process $($process.Id) did not close within five seconds; no forced termination was attempted."
                }
            }
        } catch {
            $result.cleanupPendingProcessIds += $process.Id
            Add-CleanupError "Cleanup of owned process $($process.Id) failed: $($_.Exception.Message)"
        }
        finally { $process.Dispose() }
    }
    Set-LifecycleStage 'verification:profile-unchanged'
    try {
        $profileAfter = if (Test-Path -LiteralPath $profilePath) { (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash } else { $null }
        $result.profileUnchanged = $profileBefore -eq $profileAfter
        if (-not $result.profileUnchanged) { throw 'User settings changed during the test.' }
    } catch {
        $result.status = 'failed'
        $result.profileError = $_.Exception.Message
        if (-not $result.error) { $result.error = $result.profileError; $result.failedStage = $result.stage }
    }
    $result.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    Set-LifecycleStage 'completed'
}
if ($result.status -ne 'passed') { throw $result.error }
Write-Output 'PASS: settings started on demand, reused one process, exited fully, restarted, and left the panel running.'
