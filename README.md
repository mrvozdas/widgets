# w11widget

w11widget is a compact Windows 11 x64 panel for world clocks, countdowns and focus timers. The resident panel uses C++/Win32, Direct2D and DirectWrite. Settings open in a separate C++/WinRT WinUI 3 process that exits when its window closes. The native application does not require Qt, Python or a .NET application runtime.

The interface is in Turkish. The source version is defined in [`VERSION`](VERSION). Installer and portable artifacts are built locally; this document does not advertise a published release.

## Install or run

Build the artifacts using [CONTRIBUTING.md](CONTRIBUTING.md), or use artifacts supplied with their SHA-256 manifest:

- `w11widget-<version>-installer.exe` installs under `%LOCALAPPDATA%\w11widget\app\<version>-<unique-id>\` without administrator rights.
- `w11widget-<version>-windows-x64.zip` contains a portable application directory. Extract the complete directory and run `w11widget.exe`; the adjacent settings EXE, DLLs and PRI files must remain together.

The installer retains earlier application directories and the user profile. If an installed copy is already running, it leaves that copy running and does not launch a second one. Exit the old copy from its tray menu, then run the new directory's `w11widget.exe`. To roll back the executable, exit the new copy and run the previous directory's EXE. A profile rollback is separate: close both panel and settings before restoring a known backup of `settings.json`.

The scripts do not sign artifacts, register an MSIX package, create a startup task or publish downloads. Portable mode still uses the normal user profile unless `--settings-file` explicitly selects another absolute path.

## Panel and settings

The panel's gear button or **F2** opens settings. The tray icon toggles panel visibility; its context menu provides **Göster / Gizle**, **Ayarlar** and **Çıkış**. Closing the floating panel hides it; use **Çıkış** to end the application. Closing settings leaves the panel running.

```text
w11widget.exe --settings
w11widget.exe --background
```

`--settings` opens or activates settings. `--background` starts with the panel hidden and the tray icon available. Reopening the same profile in the same user session activates the existing instance.

Settings have three pages: widgets, placement and appearance. Changes are saved together with **Uygula**; **Geri al** reloads the saved file. Theme choices include Windows, light and dark, with a separate panel opacity control.

## Placement

Select a monitor, side, width and edge offset. Compact presets provide starting dimensions. The placement preview shows the selected region; it does not detect gaps between taskbar icons.

Confirm that the chosen region does not cover Start, application icons or the clock before applying it. Confirmation is tied to the selected monitor, taskbar geometry and DPI. Changing that context requires confirmation again. When placement is unconfirmed or unavailable, the panel opens as a floating preview rather than occupying an unapproved taskbar region. Auto-hidden or unsupported taskbar layouts cannot be confirmed as normal panel placement.

Widgets that do not fit are available through the overflow menu. The settings button remains accessible.

## Widgets

The list supports up to 16 independent items. Add, remove and reorder items; set their labels, widths, enabled state and panel visibility.

| Widget | Behavior |
| --- | --- |
| World clock | Displays a 24-hour clock for an IANA time zone, with an optional date. Its popup shows date and time-zone details. |
| Countdown | Counts down to a target time in an IANA time zone, either daily or on a specified date. Its popup shows the target and remaining time. |
| Focus timer | Runs a configurable session with start, pause, resume and reset controls in its popup. |

Clock and countdown popup **Düzenle** buttons open settings. Popup windows close with Escape or loss of activation and stay within the monitor work area.

Focus sessions pause when their widget is hidden, disabled, moved into overflow, or the panel is hidden or the computer suspends. Session progress is held in memory and resets when the panel process exits. Clock and countdown values use the system clock and Windows time-zone data; no account or network connection is needed for them.

## Settings and backups

The default profile is `%LOCALAPPDATA%\w11widget\settings.json`. The panel watches this file and applies saved changes. Settings validate input, detect concurrent file changes, and save by atomic replacement; replacing an existing file creates a uniquely named backup beside it. Invalid files and conflicting edits produce an error instead of silently overwriting the profile.

Existing profiles are migrated in memory and written only when changes are applied. Unknown widget types and their settings are preserved. Installation itself does not migrate or overwrite the profile. For isolated testing, pass an absolute `--settings-file` path; the panel passes that path to its settings process.

## Development and third-party notices

See [CONTRIBUTING.md](CONTRIBUTING.md) for native builds, verification, packaging and the optional legacy Qt comparison build. The normal local gate is `./scripts/check.sh`. On macOS or Linux it verifies only the portable C++ core; the Windows application and UI require Windows verification.

Native packages include `licenses/` and `THIRD-PARTY-NOTICES.txt` copied from the pinned Windows App SDK, C++/WinRT and WebView2 packages, plus provenance for the app-local Visual C++ redistributable. `package-manifest.json` records the payload, license file hashes and static DLL dependency checks. The native package contains no Qt runtime. These notices describe third-party components; they do not assign a license to this repository's own source.
