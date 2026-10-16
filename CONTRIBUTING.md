# Contributing to w11widget

Keep changes focused and validate the resulting behavior. The native application is the production path; the previous Qt implementation remains available for comparison and regression investigation. The forge is used for source hosting only: do not add hosted CI, package publication or release automation.

## Native development environment

Use Windows 11 x64 with Visual Studio 2022 v143 C++ tools, Windows SDK `10.0.26100.0`, MSBuild, CMake 3.20 or newer and Ninja. WinUI also requires the UWP C++ and common UWP Build Tools components, even though the application is unpackaged. [`scripts/setup-native-dev.ps1`](scripts/setup-native-dev.ps1) lists the required Visual Studio components; running it installs or modifies developer tools and is separate from verification.

The WinUI project pins its NuGet dependencies in [`native/settings/packages.config`](native/settings/packages.config). Restore them once with an explicitly selected NuGet CLI:

```powershell
.\scripts\build-native.ps1 -RestorePackages -NuGetExecutable C:\Tools\nuget.exe
```

The default Visual Studio directory is `C:\BuildTools2022`; pass `-VisualStudioDirectory` for another installation. The bootstrapper follows the VS 2022 servicing channel, while the project fixes the v143 toolset and Windows SDK target. NuGet package versions are exact pins. Do not describe the Visual Studio installer as a pinned binary.

Subsequent native builds do not restore packages or access the network:

```powershell
.\scripts\build-native.ps1
```

This configures and compiles `native/`, runs CTest, then fully rebuilds the WinUI settings project. MSBuild FileTracker excludes dependency headers under `%LOCALAPPDATA%`; a full WinUI rebuild prevents stale objects from retaining an incompatible shared C++ structure layout in that development location. Outputs are `build-native-production\bin\w11widget_native_panel.exe` and `native\settings\out\x64\Release\`. `build-result.json` records the source version and artifact paths. The raw settings output is not a redistributable package until the runtime packager adds the app-local CRT and notices.

## Canonical verification

From a POSIX shell (Git Bash on Windows):

```sh
./scripts/check.sh
```

On Windows this invokes the native build above, including platform tests and the WinUI compiler. Use `W11WIDGET_VS_ROOT` to select another Visual Studio installation. On macOS or Linux it compiles and tests the framework-free C++ core and reports Windows coverage as skipped. `W11WIDGET_CHECK_BUILD_DIR` selects an alternate build directory.

The gate prints the date, commit and dirty-tree state. It does not launch interactive UI, install the application, package artifacts or publish anything. Record actual test output and distinguish skipped platform checks from passed checks.

Run WinUI interaction tests separately on an interactive Windows desktop with a new output directory:

```powershell
powershell.exe -NoProfile -MTA -File .\native\settings\test_settings_ui.ps1 `
  -SettingsExecutable .\native\settings\out\x64\Release\w11widget_native_settings.exe `
  -OutputDirectory .\build-check\native-settings-ui -CaptureWindows
```

This test uses its own profile fixture and verifies that the real user profile stays unchanged. It covers settings interactions and persistence; panel/tray/placement interaction and visual checks remain separate. Capture relevant before/after UI states and record artifact paths in the project dev-log.

After staging the complete runtime, exercise panel popups and the settings process lifecycle with a separate isolated profile:

```powershell
powershell.exe -NoProfile -MTA -File .\native\panel\test_settings_lifecycle.ps1 `
  -PanelExecutable C:\Path\To\Staged\w11widget.exe `
  -OutputDirectory .\build-check\native-panel-ui -Production -CaptureWindows
```

`-Production` is required for current application behavior; omitting it selects the retained prototype mode. This script does not replace the broader settings interaction test above.

## Build installer and portable artifacts

After a successful native build, package into a new output directory:

```powershell
.\scripts\package-native.ps1 `
  -PanelExecutable .\build-native-production\bin\w11widget_native_panel.exe `
  -SettingsDirectory .\native\settings\out\x64\Release `
  -OutputDirectory .\build-native-package `
  -SourceRevision (git rev-parse HEAD) -VerifyInstaller
```

The script reads [`VERSION`](VERSION), checks both EXEs' Windows `ProductVersion` and the panel's `--version` output, and stages the panel as `w11widget.exe`. The settings executable retains its name. Native CMake, WinUI metadata and installer versions all consume this central file; version bumps belong in their own commit.

Staging copies the self-contained WinUI output without PDB files, app-local DLLs only from the installed `VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT`, and root license/notice documents from the pinned runtime packages. `-PackagesDirectory` can select an existing restored package directory. It verifies copied hashes, x64 PE headers, required runtime resources and static DLL imports. Static imports do not prove dynamic loading or GUI behavior.

The standalone installer build under `installer/` embeds this payload without Qt. `-VerifyInstaller` checks repeated and concurrent extraction to unique version directories, preservation of a running fixture and locked old payload, profile bytes, explicit in-place extraction failure/repair, and smoke startup of the extracted copies with a clean PATH. Interactive autolaunch-suppression testing is a separate explicit option in `scripts/test-installer.ps1`; the default package verification reports it as skipped.

The output contains:

- `w11widget-<version>/`: complete runtime payload, `package-manifest.json`, file hashes and notices.
- `w11widget-<version>-installer.exe`: per-user installer retaining earlier copies.
- `w11widget-<version>-windows-x64.zip`: portable package, with each archive entry checked against the staged bytes.
- `release-manifest.json` and `SHA256SUMS`: artifact hashes and installer verification status.

An incomplete staging directory keeps `PACKAGING-INCOMPLETE.txt`. Use a new output directory after fixing a failure. Packaging does not sign, publish or install the application in the active user profile. A successful smoke test does not establish settings, placement, theme or accessibility correctness.

## Source boundaries

| Path | Responsibility |
| --- | --- |
| `native/core/` | Framework-free configuration, validation and focus state. |
| `native/platform/` | Windows profile persistence and time-zone integration. |
| `native/panel/` | Resident Win32 panel, rendering, popups, tray and process lifecycle. |
| `native/settings/` | On-demand WinUI settings and pinned build dependencies. |
| `installer/`, `scripts/` | Local builds, packaging and explicit verification. |
| `src/`, root `CMakeLists.txt` | Preserved legacy Qt implementation. |

Keep domain types independent of UI frameworks. UI source strings belong in Turkish localization files; code, comments and public documentation use English. Preserve unknown profile data, reject invalid input and avoid logging user profile contents. Only mutate the application's own windows and processes.

The package's required-file list is shared by the stager and generated installer header in `installer/native-required-files.txt`. Change this contract together with the runtime outputs. Never edit generated manifests, headers or files under `obj/`.

## Optional legacy Qt verification

Qt is not a native production dependency. To compile and test the retained Qt implementation as well as the native core:

```sh
W11WIDGET_CHECK_LEGACY_QT=1 W11WIDGET_QT_PREFIX=/path/to/Qt ./scripts/check.sh
```

The legacy target requires Qt 6.8 or newer with Core, Gui, Widgets, Qml, Quick and Quick Controls 2. Its Windows overlay still requires Windows. The gate places it under `build-check/legacy-qt`; it does not package or deploy it. Existing Qt-specific `build-panel.cmd` and installer scripts are retained for that legacy path, not the native release. Legacy screenshots and `docs/widget-api.md` describe that implementation.

## Change review

Include the behavior changed, validation performed and remaining limitations. Keep compiler/test evidence distinct from visual, installer and live-machine evidence. Do not commit build outputs, credentials, local profiles or machine-specific paths. Use English Conventional Commit subjects without attribution trailers; keep version bumps separate and create release tags only for versions that actually ship.
