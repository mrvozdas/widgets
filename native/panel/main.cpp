#include "ClockReadings.h"
#include "PanelCore.h"
#include "ProductionPanel.h"
#include "locales/tr.h"

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace w11widget::native {
namespace {
constexpr UINT_PTR MinuteTimer = 1;
constexpr UINT_PTR DurationTimer = 2;
constexpr int SettingsButton = 100;
constexpr wchar_t WindowClass[] = L"w11widget.NativeBenchmarkPanel";

template<class T> void release(T*& object) { if (object) { object->Release(); object = nullptr; } }

double unixMilliseconds()
{
    FILETIME file{};
    GetSystemTimeAsFileTime(&file);
    ULARGE_INTEGER value{};
    value.LowPart = file.dwLowDateTime;
    value.HighPart = file.dwHighDateTime;
    return static_cast<double>(value.QuadPart - 116444736000000000ULL) / 10000.0;
}

double processCpuSeconds()
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    ULARGE_INTEGER kernelValue{}, userValue{};
    kernelValue.LowPart = kernel.dwLowDateTime;
    kernelValue.HighPart = kernel.dwHighDateTime;
    userValue.LowPart = user.dwLowDateTime;
    userValue.HighPart = user.dwHighDateTime;
    return static_cast<double>(kernelValue.QuadPart + userValue.QuadPart) / 10000000.0;
}

D2D1_COLOR_F color(COLORREF value)
{
    return D2D1::ColorF(GetRValue(value) / 255.0f, GetGValue(value) / 255.0f, GetBValue(value) / 255.0f);
}

struct Panel {
    Options options;
    HWND window = nullptr;
    HWND settings = nullptr;
    HANDLE settingsProcess = nullptr;
    DWORD settingsProcessId = 0;
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* textFactory = nullptr;
    IDWriteTextFormat* textFormat = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    HFONT buttonFont = nullptr;
    ClockSnapshot clocks;
    unsigned paints = 0;
    unsigned refreshes = 0;
    bool dark = false;
    bool highContrast = false;
    bool runtimeFailed = false;
    std::string exitReason = "window-closed";
    D2D1_COLOR_F background{};
    D2D1_COLOR_F tile{};
    D2D1_COLOR_F foreground{};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    double startedCpu = processCpuSeconds();
    double readyMilliseconds = 0;

    ~Panel()
    {
        if (settingsProcess) CloseHandle(settingsProcess);
        if (buttonFont) DeleteObject(buttonFont);
        release(brush); release(target); release(textFormat); release(textFactory); release(factory);
    }

    bool initializeGraphics()
    {
        HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
        if (SUCCEEDED(result)) result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&textFactory));
        if (SUCCEEDED(result)) result = textFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f, L"tr-TR", &textFormat);
        if (SUCCEEDED(result)) {
            textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (FAILED(result)) std::cerr << "GRAPHICS_INIT_FAILED " << static_cast<unsigned long>(result) << '\n';
        return SUCCEEDED(result);
    }

    void updateTheme()
    {
        DWORD light = 1;
        DWORD bytes = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
        HIGHCONTRASTW contrast{sizeof(contrast)};
        highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        dark = light == 0 && !highContrast;
        background = highContrast ? color(GetSysColor(COLOR_WINDOW)) :
            D2D1::ColorF(dark ? 0x202020 : 0xf3f3f3);
        tile = highContrast ? color(GetSysColor(COLOR_WINDOW)) : D2D1::ColorF(dark ? 0x2b2b2b : 0xffffff);
        foreground = highContrast ? color(GetSysColor(COLOR_WINDOWTEXT)) : D2D1::ColorF(dark ? 0xf5f5f5 : 0x1a1a1a);
        if (window) {
            const BOOL enabled = dark;
            DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
            const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
            DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
            InvalidateRect(window, nullptr, FALSE);
        }
    }

    void scheduleMinute()
    {
        KillTimer(window, MinuteTimer);
        if (!IsWindowVisible(window) || IsIconic(window)) return;
        SYSTEMTIME now{};
        GetSystemTime(&now);
        const auto delay = millisecondsToNextMinute(now.wSecond * 1000U + now.wMilliseconds);
        if (!SetTimer(window, MinuteTimer, delay, nullptr)) {
            runtimeFailed = true;
            std::cerr << "MINUTE_TIMER_FAILED " << GetLastError() << '\n';
        }
    }

    void refresh()
    {
        clocks = readClocks(unixMilliseconds());
        ++refreshes;
        if (!clocks.valid) std::cerr << "CLOCK_ERROR " << clocks.error << '\n';
        InvalidateRect(window, nullptr, FALSE);
        scheduleMinute();
    }

    void resize()
    {
        RECT bounds{};
        GetClientRect(window, &bounds);
        const UINT dpi = GetDpiForWindow(window);
        const int buttonWidth = MulDiv(76, dpi, 96);
        const int margin = MulDiv(4, dpi, 96);
        MoveWindow(settings, std::max(0L, bounds.right - buttonWidth - margin), margin,
            buttonWidth, std::max(1L, bounds.bottom - margin * 2), TRUE);
        if (buttonFont) DeleteObject(buttonFont);
        buttonFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI Variable");
        SendMessageW(settings, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont), TRUE);
        if (target) {
            target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
            target->Resize(D2D1::SizeU(static_cast<UINT32>(std::max(0L, bounds.right)),
                static_cast<UINT32>(std::max(0L, bounds.bottom))));
        }
        InvalidateRect(window, nullptr, FALSE);
    }

    bool ensureTarget()
    {
        if (target) return true;
        RECT bounds{};
        GetClientRect(window, &bounds);
        const UINT dpi = GetDpiForWindow(window);
        auto properties = D2D1::RenderTargetProperties();
        properties.dpiX = properties.dpiY = static_cast<float>(dpi);
        HRESULT result = factory->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(bounds.right, bounds.bottom)), &target);
        if (SUCCEEDED(result)) result = target->CreateSolidColorBrush(foreground, &brush);
        if (FAILED(result)) { release(brush); release(target); }
        return SUCCEEDED(result);
    }

    void paint()
    {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        if (ensureTarget()) {
            target->BeginDraw();
            target->Clear(background);
            const auto size = target->GetSize();
            const float width = std::max(0.0f, (size.width - 88.0f) / 4.0f);
            const std::array<std::wstring, 4> labels{strings::Countdown, L"UTC", strings::NewYork, strings::Istanbul};
            for (std::size_t index = 0; index < labels.size(); ++index) {
                const float left = 4 + static_cast<float>(index) * width;
                const D2D1_RECT_F bounds{left, 3, left + width - 4, size.height - 3};
                brush->SetColor(tile);
                target->FillRoundedRectangle(D2D1::RoundedRect(bounds, 4, 4), brush);
                brush->SetColor(foreground);
                const auto text = labels[index] + L"  " + (clocks.valid ? clocks.values[index] : L"—");
                target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), textFormat,
                    D2D1::RectF(bounds.left + 7, bounds.top, bounds.right - 5, bounds.bottom), brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            const HRESULT result = target->EndDraw();
            if (result == D2DERR_RECREATE_TARGET) { release(brush); release(target); }
            else if (SUCCEEDED(result)) ++paints;
            else { runtimeFailed = true; std::cerr << "DRAW_FAILED " << static_cast<unsigned long>(result) << '\n'; }
        } else {
            runtimeFailed = true;
            std::cerr << "RENDER_TARGET_FAILED\n";
        }
        EndPaint(window, &paint);
    }

    void openSettings()
    {
        if (settingsProcess && WaitForSingleObject(settingsProcess, 0) == WAIT_TIMEOUT) {
            EnumWindows([](HWND candidate, LPARAM context) -> BOOL {
                DWORD process = 0;
                GetWindowThreadProcessId(candidate, &process);
                if (process != static_cast<DWORD>(context) || !IsWindowVisible(candidate)) return TRUE;
                ShowWindow(candidate, SW_RESTORE);
                SetForegroundWindow(candidate);
                return FALSE;
            }, static_cast<LPARAM>(settingsProcessId));
            return;
        }
        if (settingsProcess) { CloseHandle(settingsProcess); settingsProcess = nullptr; }
        std::wstring path(32768, L'\0');
        const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (count == 0 || count >= path.size()) return;
        path.resize(count);
        const auto executable = std::filesystem::path(path).parent_path() / L"w11widget_native_settings.exe";
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(executable, fileError)) {
            MessageBoxW(window, strings::MissingSettings, strings::Title, MB_OK | MB_ICONINFORMATION);
            return;
        }
        const auto isolatedProfile = std::filesystem::temp_directory_path() /
            (L"w11widget-reference-" + std::to_wstring(GetCurrentProcessId())) / L"settings.json";
        std::wstring command = L"\"" + executable.wstring() + L"\" --settings-file \"" + isolatedProfile.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                executable.parent_path().c_str(), &startup, &process)) {
            const auto message = std::wstring(strings::LaunchError) + std::to_wstring(GetLastError());
            MessageBoxW(window, message.c_str(), strings::Title, MB_OK | MB_ICONERROR);
            return;
        }
        CloseHandle(process.hThread);
        settingsProcess = process.hProcess;
        settingsProcessId = process.dwProcessId;
        std::cout << "SETTINGS_STARTED pid=" << settingsProcessId << '\n' << std::flush;
    }

    bool writeMetrics()
    {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double finalCpu = processCpuSeconds();
        const bool cpuAvailable = startedCpu >= 0 && finalCpu >= 0;
        const double cpuSeconds = cpuAvailable ? finalCpu - startedCpu : 0;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        const bool memoryAvailable = GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
        DWORD handles = 0;
        const bool handlesAvailable = GetProcessHandleCount(GetCurrentProcess(), &handles) != FALSE;
        std::ostringstream json;
        json << std::fixed << std::setprecision(4)
            << "{\"prototype\":\"native-panel-0.0.1\",\"pid\":" << GetCurrentProcessId()
            << ",\"scope\":\"panel-process-only\",\"widget_count\":4,\"benchmark\":" << (options.benchmark ? "true" : "false")
            << ",\"includes_startup\":true,\"memory_snapshot\":\"after-window-close\""
            << ",\"elapsed_seconds\":" << elapsed << ",\"ready_milliseconds\":" << readyMilliseconds
            << ",\"cpu_available\":" << (cpuAvailable ? "true" : "false") << ",\"cpu_seconds\":" << cpuSeconds
            << ",\"cpu_percent_single_core\":" << (elapsed > 0 ? cpuSeconds / elapsed * 100 : 0)
            << ",\"memory_available\":" << (memoryAvailable ? "true" : "false")
            << ",\"private_bytes\":" << memory.PrivateUsage << ",\"working_set_bytes\":" << memory.WorkingSetSize
            << ",\"peak_working_set_bytes\":" << memory.PeakWorkingSetSize
            << ",\"handles_available\":" << (handlesAvailable ? "true" : "false") << ",\"handle_count\":" << handles
            << ",\"paint_count\":" << paints << ",\"clock_refresh_count\":" << refreshes
            << ",\"clock_data_valid\":" << (clocks.valid ? "true" : "false")
            << ",\"runtime_failed\":" << (runtimeFailed ? "true" : "false")
            << ",\"exit_reason\":\"" << exitReason << "\"}";
        std::cout << json.str() << '\n' << std::flush;
        if (options.metricsFile.empty()) return true;
        std::ofstream output(std::filesystem::path(options.metricsFile), std::ios::binary | std::ios::trunc);
        output << json.str() << '\n';
        output.close();
        if (!output) std::cerr << "METRICS_WRITE_FAILED\n";
        return static_cast<bool>(output);
    }
};

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        panel = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        panel->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel));
    }
    if (!panel) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        panel->settings = CreateWindowExW(0, L"BUTTON", strings::Settings, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 76, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(SettingsButton)), GetModuleHandleW(nullptr), nullptr);
        return panel->settings ? 0 : -1;
    case WM_COMMAND:
        if (LOWORD(wParam) == SettingsButton && HIWORD(wParam) == BN_CLICKED) panel->openSettings();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F2) { panel->openSettings(); return 0; }
        if (wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
        break;
    case WM_TIMER:
        if (wParam == MinuteTimer) panel->refresh();
        if (wParam == DurationTimer) { panel->exitReason = "duration-complete"; DestroyWindow(window); }
        return 0;
    case WM_PAINT: panel->paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) KillTimer(window, MinuteTimer);
        else { panel->resize(); panel->refresh(); }
        return 0;
    case WM_SHOWWINDOW:
        if (wParam) panel->refresh(); else KillTimer(window, MinuteTimer);
        break;
    case WM_TIMECHANGE: panel->refresh(); return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) panel->refresh();
        return TRUE;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED: panel->updateTheme(); return 0;
    case WM_DPICHANGED: {
        const RECT& bounds = *reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        panel->resize();
        return 0;
    }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        KillTimer(window, MinuteTimer); KillTimer(window, DurationTimer);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int run(HINSTANCE instance)
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments) return 2;
    std::vector<std::wstring> values;
    for (int index = 1; index < argumentCount; ++index) values.emplace_back(arguments[index]);
    LocalFree(arguments);
    std::wstring error;
    auto options = parseOptions(values, error);
    if (!options) { std::wcerr << error << '\n'; return 2; }
    if (options->help) {
        std::cout << "w11widget " << W11WIDGET_VERSION << " native Windows panel\n"
            "--settings | --background [--settings-file ABSOLUTE_PATH]\n"
            "--duration-seconds 1..86400 --metrics-file PATH\n"
            "--benchmark (isolated reference renderer) | --self-test | --smoke-test | --version | --help\n";
        return 0;
    }
    if (options->version) { std::cout << W11WIDGET_VERSION << '\n'; return 0; }
    if (options->smokeTest) {
        if (!testTimeZones()) return 1;
        Panel probe;
        if (!probe.initializeGraphics()) return 1;
        std::cout << "Native production smoke: PASS\n";
        return 0;
    }
    if (options->selfTest) return testTimeZones() ? 0 : 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!options->benchmark) return runProductionPanel(instance, *options);
    Panel panel;
    panel.options = *options;
    if (!panel.initializeGraphics()) return 1;
    panel.clocks = readClocks(unixMilliseconds());
    if (!panel.clocks.valid) { std::cerr << panel.clocks.error << '\n'; return 1; }
    panel.updateTheme();
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const UINT dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(620, dpi, 96), MulDiv(32, dpi, 96)};
    AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const std::wstring title = std::wstring(strings::Title) + L" — " + strings::Subtitle;
    if (!CreateWindowExW(0, WindowClass, title.c_str(), style,
        work.left + std::max(0L, (work.right - work.left - width) / 2), work.top + MulDiv(80, dpi, 96),
        width, height, nullptr, nullptr, instance, &panel)) return 1;
    panel.updateTheme();
    panel.resize();
    ShowWindow(panel.window, SW_SHOWNORMAL);
    UpdateWindow(panel.window);
    panel.scheduleMinute();
    if (options->durationSeconds > 0 &&
        !SetTimer(panel.window, DurationTimer, options->durationSeconds * 1000U, nullptr)) {
        panel.runtimeFailed = true;
        panel.exitReason = "duration-timer-error";
        std::cerr << "DURATION_TIMER_FAILED " << GetLastError() << '\n';
        DestroyWindow(panel.window);
    }
    panel.readyMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - panel.started).count();
    std::cout << "WINDOW_READY pid=" << GetCurrentProcessId() << " widgets=4 prototype=native-panel-0.0.1\n" << std::flush;
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(panel.window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (result < 0) panel.exitReason = "message-loop-error";
    return panel.writeMetrics() && result == 0 && !panel.runtimeFailed && panel.clocks.valid && panel.paints > 0 ? 0 : 1;
}
} // namespace
} // namespace w11widget::native

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try { return w11widget::native::run(instance); }
    catch (const std::exception&) { std::cerr << "NATIVE_APPLICATION_ERROR\n"; return 1; }
}
