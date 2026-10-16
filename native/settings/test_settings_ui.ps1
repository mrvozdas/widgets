#requires -Version 5.1
param(
    [Parameter(Mandatory = $true)][string]$SettingsExecutable,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [switch]$CaptureWindows
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Desktop' -or [Threading.Thread]::CurrentThread.GetApartmentState() -ne [Threading.ApartmentState]::MTA) {
    throw 'Run in an interactive desktop using Windows PowerShell -NoProfile -MTA.'
}
if ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) { throw 'An interactive desktop is required.' }
$executable = (Get-Item -LiteralPath $SettingsExecutable).FullName
if ([IO.Path]::GetFileName($executable) -ne 'w11widget_native_settings.exe') { throw 'Select the native settings executable.' }
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $outputPath) { throw 'Use a new output directory; existing evidence is never overwritten.' }
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$profile = Join-Path $outputPath 'isolated-settings.json'
$normalProfile = Join-Path $env:LOCALAPPDATA 'w11widget\settings.json'
$normalBefore = if (Test-Path -LiteralPath $normalProfile) { (Get-FileHash -LiteralPath $normalProfile).Hash } else { $null }
$labels = @{}
foreach ($match in [regex]::Matches([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'strings\tr-TR.h')), 'inline constexpr auto (\w+) = L"([^"\\]*)";')) {
    $labels[$match.Groups[1].Value] = $match.Groups[2].Value
}
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -ReferencedAssemblies ([Windows.Automation.ClientSettings].Assembly.Location) -TypeDefinition @'
using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
public static class SettingsUiClient {
    private delegate bool EnumProc(IntPtr hwnd, IntPtr unused);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd, out Rect bounds);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr unused);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint id);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [StructLayout(LayoutKind.Sequential)] private struct CopyData { public UIntPtr Tag; public uint Bytes; public IntPtr Data; }
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SendMessageTimeoutW(IntPtr hwnd, uint message, IntPtr sender, ref CopyData data, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll")] private static extern bool PostMessageW(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);
    public static IntPtr FindWindow(int id) {
        IntPtr result = IntPtr.Zero;
        long largestArea = 0;
        EnumWindows(delegate(IntPtr hwnd, IntPtr unused) {
            uint owner; int cloaked;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == id && IsWindowVisible(hwnd) && !IsIconic(hwnd) && (DwmGetWindowAttribute(hwnd, 14, out cloaked, 4) != 0 || cloaked == 0)) {
                Rect bounds;
                if (GetWindowRect(hwnd, out bounds)) {
                    long area = ((long)bounds.Right - bounds.Left) * ((long)bounds.Bottom - bounds.Top);
                    if (area > largestArea) { result = hwnd; largestArea = area; }
                }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static bool SelectWidget(int processId, string widgetId) {
        IntPtr buffer = Marshal.StringToHGlobalUni(widgetId);
        try {
            CopyData data = new CopyData { Tag = new UIntPtr(0x57313157), Bytes = checked((uint)((widgetId.Length + 1) * 2)), Data = buffer };
            UIntPtr result;
            return SendMessageTimeoutW(FindWindow(processId), 0x4A, IntPtr.Zero, ref data, 2, 2000, out result) != IntPtr.Zero && result.ToUInt64() == 1;
        } finally { Marshal.FreeHGlobal(buffer); }
    }
    public static void Close(int id) {
        IntPtr hwnd = FindWindow(id);
        if (hwnd != IntPtr.Zero) PostMessageW(hwnd, 0x10, IntPtr.Zero, IntPtr.Zero);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static Assembly RegisterProviders() {
        AssemblyName client = typeof(System.Windows.Automation.ClientSettings).Assembly.GetName();
        AssemblyName name = new AssemblyName("UIAutomationClientsideProviders");
        name.Version = client.Version; name.CultureInfo = client.CultureInfo; name.SetPublicKeyToken(client.GetPublicKeyToken());
        Assembly providers = Assembly.Load(name);
        System.Windows.Automation.ClientSettings.RegisterClientSideProviderAssembly(providers.GetName());
        return providers;
    }
}
'@
[SettingsUiClient]::RegisterProviders() | Out-Null
$result = [ordered]@{ status = 'running'; stage = 'initialized'; checks = @(); processIds = @(); error = $null; cleanupErrors = @(); normalProfileUnchanged = $false }
$owned = New-Object 'System.Collections.Generic.List[System.Diagnostics.Process]'
$process = $null
function Stage([string]$Name) {
    $result.stage = $Name
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'settings-ui.json') -Encoding UTF8
}
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Write-Profile($Value) { [IO.File]::WriteAllText($profile, ($Value | ConvertTo-Json -Depth 15), [Text.UTF8Encoding]::new($false)) }
function Start-Settings([string]$WidgetId = 'utc-1') {
    $info = [Diagnostics.ProcessStartInfo]::new($executable)
    $info.UseShellExecute = $false
    $info.Arguments = '--settings-file "' + $profile + '" --widget-id "' + $WidgetId + '"'
    $started = [Diagnostics.Process]::Start($info)
    $null = $started.Handle
    $owned.Add($started)
    $result.processIds += $started.Id
    $script:process = $started
    $null = Window
}
function Window {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        if ($process.HasExited) { throw "Settings exited early: $($process.ExitCode)" }
        $handle = [SettingsUiClient]::FindWindow($process.Id)
        if ($handle -ne [IntPtr]::Zero) {
            $window = [Windows.Automation.AutomationElement]::FromHandle($handle)
            if ($window.Current.ProcessId -eq $process.Id) { return $window }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Owned settings window not found within 20 seconds.'
}
function Find([string]$Name, [Windows.Automation.ControlType]$Type) {
    Stage "find:${Name}:$($Type.ProgrammaticName)"
    $condition = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, $Type))
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $matches = (Window).FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        if ($matches.Count -eq 1 -and $matches[0].Current.ProcessId -eq $process.Id) { return $matches[0] }
        if ($matches.Count -gt 1) { throw "Ambiguous control: $Name" }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Missing control: $Name"
}
function Click([string]$Name) {
    $control = Find $Name ([Windows.Automation.ControlType]::Button)
    Assert $control.Current.IsEnabled "Disabled button: $Name"
    Stage "invoke:$Name"
    ([Windows.Automation.InvokePattern]$control.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke()
}
function Edit([string]$Name, [string]$Value) {
    $control = Find $Name ([Windows.Automation.ControlType]::Edit)
    Stage "edit:$Name"
    ([Windows.Automation.ValuePattern]$control.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)).SetValue($Value)
}
function Toggle([string]$Name) {
    $matches = (Window).FindAll([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
    $patterns = @()
    foreach ($control in $matches) {
        $pattern = $null
        if ($control.Current.ProcessId -eq $process.Id -and $control.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) { $patterns += $pattern }
    }
    Assert ($patterns.Count -eq 1) "Expected one toggle: $Name"
    Stage "toggle:$Name"
    ([Windows.Automation.TogglePattern]$patterns[0]).Toggle()
}
function Navigate([string]$Name) {
    Stage "navigate:$Name"
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $matches = (Window).FindAll([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
        $patterns = @()
        foreach ($control in $matches) {
            $pattern = $null
            if ($control.Current.ProcessId -eq $process.Id -and $control.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern, [ref]$pattern)) { $patterns += $pattern }
        }
        if ($patterns.Count -eq 1) {
            ([Windows.Automation.SelectionItemPattern]$patterns[0]).Select()
            return
        }
        if ($patterns.Count -gt 1) { throw "Ambiguous navigation item: $Name" }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected one navigation item: $Name (matches=$($matches.Count), selection patterns=$($patterns.Count))"
}
function Choose([string]$Name, [string]$Value) {
    $control = Find $Name ([Windows.Automation.ControlType]::ComboBox)
    Stage "expand:$Name"
    $expansion = [Windows.Automation.ExpandCollapsePattern]$control.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
    $expansion.Expand()
    $condition = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Value),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem))
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        # Scope selection to the expanded control; collapsed templates may also expose matching names.
        $matches = @($control.FindAll([Windows.Automation.TreeScope]::Descendants, $condition) | Where-Object { -not $_.Current.IsOffscreen })
        if ($matches.Count -eq 0) {
            $matches = @((Window).FindAll([Windows.Automation.TreeScope]::Descendants, $condition) | Where-Object { -not $_.Current.IsOffscreen })
        }
        if ($matches.Count -eq 1) {
            Assert ($matches[0].Current.ProcessId -eq $process.Id) 'Combo item ownership changed.'
            ([Windows.Automation.SelectionItemPattern]$matches[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern)).Select()
            $expansion.Collapse()
            return
        }
        if ($matches.Count -gt 1) {
            $diagnostic = @($matches | ForEach-Object { @{ name = $_.Current.Name; className = $_.Current.ClassName; offscreen = $_.Current.IsOffscreen; runtimeId = $_.GetRuntimeId(); bounds = $_.Current.BoundingRectangle.ToString() } })
            $diagnostic | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'combo-ambiguity.json')
            throw "Ambiguous visible combo option: $Name / $Value"
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Missing visible combo option: $Name / $Value"
}
function Names([int]$Count) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $list = Find $labels.widgets ([Windows.Automation.ControlType]::List)
        $items = $list.FindAll([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem))
        if ($items.Count -eq $Count) { return @($items | ForEach-Object { $_.Current.Name }) }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected $Count widget rows; got $($items.Count)."
}
function Saved {
    $null = Find $labels.savedTitle ([Windows.Automation.ControlType]::Text)
    return Get-Content -LiteralPath $profile -Raw -Encoding UTF8 | ConvertFrom-Json
}
function Close-Settings {
    Stage 'close-owned-settings'
    ([Windows.Automation.WindowPattern](Window).GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern)).Close()
    Assert ($process.WaitForExit(10000)) 'Settings did not exit after close.'
    Assert ($process.ExitCode -eq 0) "Settings exit code: $($process.ExitCode)"
}

$fixture = [ordered]@{
    schema_version = 2; future_root = @{ nested = @('keep', 17) }
    panel = @{ monitor = 'primary'; side = 'left'; width_dip = 330; offset_dip = 150; opacity = 0.95; hide_for_fullscreen = $false; theme = 'system'; region_confirmed = $false; future_panel = 'keep'; region_context = @{ monitor = '\\.\DISPLAY1'; monitor_left = 0; monitor_top = 0; monitor_right = 1920; monitor_bottom = 1080; taskbar_left = 0; taskbar_top = 1040; taskbar_right = 1920; taskbar_bottom = 1080; dpi = 96; future_region = 'keep' } }
    widgets = @(
        @{ instance_id = 'countdown-1'; type_id = 'countdown'; enabled = $true; visible = $true; width_dip = 72; settings = @{ label = $labels.countdown; target_time = '18:00'; daily_repeat = $true; target_date = ''; time_zone = 'Europe/Istanbul' } },
        @{ instance_id = 'utc-1'; type_id = 'world-clock'; enabled = $true; visible = $true; width_dip = 72; future_widget = 23; settings = @{ time_zone = 'UTC'; show_date = $false; future_setting = @{ keep = $true } } },
        @{ instance_id = 'ny-1'; type_id = 'world-clock'; enabled = $true; visible = $true; width_dip = 72; settings = @{ time_zone = 'America/New_York'; show_date = $false } },
        @{ instance_id = 'focus-1'; type_id = 'focus-timer'; enabled = $true; visible = $true; width_dip = 72; settings = @{ label = $labels.focusLabel; duration_minutes = 25 } },
        @{ instance_id = 'future-1'; type_id = 'future-widget'; enabled = $true; visible = $false; width_dip = 72; settings = @{ opaque = @('preserve', 42) }; future_flag = 'keep' }
    )
}
try {
    Stage 'write-isolated-fixture'
    Write-Profile $fixture
    Start-Settings
    $initial = @(Names 5)
    Click $labels.addWidget
    $duplicates = @(Names 6)
    Assert (@($duplicates | Where-Object { $_ -eq 'UTC' }).Count -eq 2) 'Equal UTC rows were not independently accessible.'
    $result.checks += 'duplicate UTC UIA identity'
    Edit $labels.displayName 'Saved UI clock'
    Edit $labels.timezone 'Asia/Tokyo'
    Toggle $labels.enabled
    Toggle $labels.visible
    Toggle $labels.showDate
    Navigate $labels.layout
    $null = Find $labels.monitor ([Windows.Automation.ControlType]::ComboBox)
    $confirmation = Find $labels.confirmRegion ([Windows.Automation.ControlType]::CheckBox)
    Assert (-not $confirmation.Current.IsOffscreen) 'Region confirmation is not visible in the layout page.'
    $result.checks += 'region confirmation visible'
    Choose $labels.align $labels.right
    Click $labels.presetLeft
    # A test must not assert that a real taskbar area is unoccupied on the user's behalf.
    Navigate $labels.appearance
    Choose $labels.theme $labels.darkTheme
    Navigate $labels.widgets
    $edited = @(Names 6)
    Assert ($edited[5] -eq 'Saved UI clock') 'Edits were lost during navigation.'
    Click $labels.moveUp
    $up = @(Names 6)
    Assert ($up[4] -eq 'Saved UI clock' -and $up[5] -eq $edited[4]) 'Move up did not reorder the exact widget.'
    Click $labels.moveDown
    Assert (((Names 6) -join '|') -eq ($edited -join '|')) 'Move down did not restore order.'
    Click $labels.apply
    $saved = Saved
    $newWidget = @($saved.widgets | Where-Object { $_.settings.PSObject.Properties['label'] -and $_.settings.label -eq 'Saved UI clock' })[0]
    Assert ($saved.widgets.Count -eq 6 -and $newWidget.settings.time_zone -eq 'Asia/Tokyo' -and -not $newWidget.enabled -and -not $newWidget.visible -and $newWidget.settings.show_date) 'Typed widget changes were not persisted.'
    Assert ($saved.panel.theme -eq 'dark' -and $saved.panel.side -eq 'left' -and -not $saved.panel.region_confirmed) 'Theme, preset or explicit-confirmation state did not persist.'
    Assert ($saved.future_root.nested[0] -eq 'keep' -and $saved.panel.future_panel -eq 'keep' -and $saved.panel.region_context.future_region -eq 'keep' -and $saved.widgets[1].future_widget -eq 23 -and $saved.widgets[1].settings.future_setting.keep -and $saved.widgets[4].settings.opaque[1] -eq 42) 'Unknown configuration fields were lost.'
    $result.checks += @('apply typed values', 'enable and visibility', 'reorder', 'theme and placement preset', 'unknown fields preserved')
    $savedBytes = [IO.File]::ReadAllText($profile)
    Edit $labels.displayName 'Unsaved edit'
    Click $labels.revert
    Assert ((Names 6)[5] -eq 'Saved UI clock') 'Revert did not restore disk configuration.'
    Assert ([IO.File]::ReadAllText($profile) -eq $savedBytes) 'Revert modified the profile.'
    Edit $labels.timezone 'Invalid/Not_A_Zone'
    Click $labels.apply
    $null = Find $labels.invalidTitle ([Windows.Automation.ControlType]::Text)
    Assert ([IO.File]::ReadAllText($profile) -eq $savedBytes) 'An invalid timezone overwrote valid settings.'
    Click $labels.revert
    $result.checks += @('revert', 'invalid timezone rejected')
    Edit $labels.displayName 'Stale writer'
    $external = Get-Content -LiteralPath $profile -Raw -Encoding UTF8 | ConvertFrom-Json
    $external | Add-Member -NotePropertyName external_revision -NotePropertyValue 7
    Write-Profile $external
    $externalBytes = [IO.File]::ReadAllText($profile)
    Click $labels.apply
    $null = Find $labels.conflictTitle ([Windows.Automation.ControlType]::Text)
    Assert ([IO.File]::ReadAllText($profile) -eq $externalBytes) 'Stale save overwrote external changes.'
    Click $labels.revert
    $result.checks += 'stale revision rejected'
    foreach ($page in @('widgets', 'layout', 'appearance')) {
        Navigate $labels[$page]
        if ($CaptureWindows) { & (Join-Path $PSScriptRoot '..\..\scripts\capture-window.ps1') -ProcessId $process.Id -OutputPng (Join-Path $outputPath "settings-$page.png") | Out-Null }
    }
    Close-Settings
    Start-Settings $newWidget.instance_id
    Assert ((Names 6)[5] -eq 'Saved UI clock') 'Saved configuration did not survive process restart.'
    $label = Find $labels.displayName ([Windows.Automation.ControlType]::Edit)
    Assert (([Windows.Automation.ValuePattern]$label.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)).Current.Value -eq 'Saved UI clock') '--widget-id did not select the requested widget.'
    Assert ([SettingsUiClient]::SelectWidget($process.Id, 'focus-1')) 'Existing-window focus selection message was rejected.'
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $currentLabel = Find $labels.displayName ([Windows.Automation.ControlType]::Edit)
        $selectedLabel = ([Windows.Automation.ValuePattern]$currentLabel.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)).Current.Value
        if ($selectedLabel -eq $labels.focusLabel) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    Assert ($selectedLabel -eq $labels.focusLabel) 'Existing-window message did not select the focus widget.'
    Assert (-not [SettingsUiClient]::SelectWidget($process.Id, '../invalid')) 'Invalid widget identity was accepted.'
    Assert ([SettingsUiClient]::SelectWidget($process.Id, $newWidget.instance_id)) 'Existing-window clock selection message was rejected.'
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $currentLabel = Find $labels.displayName ([Windows.Automation.ControlType]::Edit)
        $selectedLabel = ([Windows.Automation.ValuePattern]$currentLabel.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)).Current.Value
        if ($selectedLabel -eq 'Saved UI clock') { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    Assert ($selectedLabel -eq 'Saved UI clock') 'Existing-window message did not restore clock selection.'
    $result.checks += 'existing-window validated widget selection'
    Click $labels.remove
    Click $labels.apply
    $removed = Saved
    Assert ($removed.widgets.Count -eq 5) 'Removal did not persist.'
    Close-Settings
    $result.checks += @('process restart and widget selection', 'remove persisted', 'graceful close')
    [IO.File]::WriteAllText($profile, '{broken json', [Text.UTF8Encoding]::new($false))
    Start-Settings
    $null = Find $labels.loadFailedTitle ([Windows.Automation.ControlType]::Text)
    Assert (-not (Find $labels.apply ([Windows.Automation.ControlType]::Button)).Current.IsEnabled) 'Apply was enabled after a load error.'
    Close-Settings
    Assert ([IO.File]::ReadAllText($profile) -eq '{broken json') 'Malformed profile was overwritten.'
    $result.checks += 'load error never overwrites'
    $result.status = 'passed'
} catch {
    $result.status = 'failed'
    $result.error = $_.ToString() + "`n" + $_.ScriptStackTrace
    if ($CaptureWindows -and $process -and -not $process.HasExited) {
        try { & (Join-Path $PSScriptRoot '..\..\scripts\capture-window.ps1') -ProcessId $process.Id -OutputPng (Join-Path $outputPath 'settings-failure.png') | Out-Null } catch { }
    }
} finally {
    foreach ($item in $owned) {
        try {
            if (-not $item.HasExited) { [SettingsUiClient]::Close($item.Id); if (-not $item.WaitForExit(10000)) { throw "Owned PID $($item.Id) did not exit after WM_CLOSE." } }
            $item.Dispose()
        } catch { $result.cleanupErrors += $_.ToString(); $result.status = 'failed' }
    }
    $normalAfter = if (Test-Path -LiteralPath $normalProfile) { (Get-FileHash -LiteralPath $normalProfile).Hash } else { $null }
    $result.normalProfileUnchanged = $normalBefore -eq $normalAfter
    if (-not $result.normalProfileUnchanged) { $result.status = 'failed'; $result.error = 'The default user profile changed during an isolated UI test.' }
    Stage $result.stage
}
$result | ConvertTo-Json -Depth 8
if ($result.status -ne 'passed') { exit 1 }
