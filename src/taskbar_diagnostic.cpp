#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return "";
    }

    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size, nullptr,
                        nullptr);
    return utf8;
}

std::string edgeToString(UINT edge) {
    switch (edge) {
    case ABE_LEFT:
        return "left";
    case ABE_TOP:
        return "top";
    case ABE_RIGHT:
        return "right";
    case ABE_BOTTOM:
        return "bottom";
    default:
        return "unknown";
    }
}

std::string rectToString(const RECT& rectangle) {
    return "{left=" + std::to_string(rectangle.left) + ", top=" + std::to_string(rectangle.top) +
           ", right=" + std::to_string(rectangle.right) + ", bottom=" +
           std::to_string(rectangle.bottom) + ", width=" +
           std::to_string(rectangle.right - rectangle.left) + ", height=" +
           std::to_string(rectangle.bottom - rectangle.top) + "}";
}

std::string win32Error(DWORD errorCode) {
    if (errorCode == 0) {
        return "error=0 (no extended Win32 error information)";
    }

    LPWSTR messageBuffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);

    std::wstring message = size > 0 && messageBuffer != nullptr ? std::wstring(messageBuffer, size)
                                                                 : L"Unknown Win32 error";

    if (messageBuffer != nullptr) {
        LocalFree(messageBuffer);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'\0')) {
        message.pop_back();
    }

    return "error=" + std::to_string(errorCode) + " (" + toUtf8(message) + ")";
}

struct DpiResult {
    bool ok{false};
    UINT dpiX{0};
    UINT dpiY{0};
    std::string source;
    std::string error;
};

DpiResult detectDpi(HWND taskbarWindow, HMONITOR monitor) {
    DpiResult result;

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"); user32 != nullptr) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto getDpiForWindowFn =
            reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));

        if (getDpiForWindowFn != nullptr && taskbarWindow != nullptr) {
            const UINT dpi = getDpiForWindowFn(taskbarWindow);
            if (dpi != 0) {
                result.ok = true;
                result.dpiX = dpi;
                result.dpiY = dpi;
                result.source = "GetDpiForWindow(taskbar)";
                return result;
            }
        }
    }

    if (HMODULE shcore = LoadLibraryW(L"shcore.dll"); shcore != nullptr) {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        auto getDpiForMonitorFn =
            reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));

        if (getDpiForMonitorFn != nullptr && monitor != nullptr) {
            UINT dpiX = 0;
            UINT dpiY = 0;
            const HRESULT hr = getDpiForMonitorFn(monitor, 0, &dpiX, &dpiY);
            if (SUCCEEDED(hr) && dpiX != 0 && dpiY != 0) {
                result.ok = true;
                result.dpiX = dpiX;
                result.dpiY = dpiY;
                result.source = "GetDpiForMonitor(MDT_EFFECTIVE_DPI)";
                FreeLibrary(shcore);
                return result;
            }
            result.error = "GetDpiForMonitor failed with HRESULT=" + std::to_string(static_cast<long>(hr));
        }
        FreeLibrary(shcore);
    }

    if (HDC screenDc = GetDC(nullptr); screenDc != nullptr) {
        const int dpiX = GetDeviceCaps(screenDc, LOGPIXELSX);
        const int dpiY = GetDeviceCaps(screenDc, LOGPIXELSY);
        ReleaseDC(nullptr, screenDc);

        if (dpiX > 0 && dpiY > 0) {
            result.ok = true;
            result.dpiX = static_cast<UINT>(dpiX);
            result.dpiY = static_cast<UINT>(dpiY);
            result.source = "GetDeviceCaps(LOGPIXELSX/LOGPIXELSY) fallback";
            return result;
        }
    }

    if (result.error.empty()) {
        result.error = "Unable to detect DPI using Win32 APIs";
    }

    return result;
}

bool runSelfTest() {
    const double scale100 = 100.0 * static_cast<double>(96) / 96.0;
    const double scale150 = 100.0 * static_cast<double>(144) / 96.0;

    const bool pass =
        std::llround(scale100) == 100 && std::llround(scale150) == 150 && edgeToString(ABE_TOP) == "top";

    std::cout << (pass ? "SELF-TEST: PASS" : "SELF-TEST: FAIL") << '\n';
    return pass;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc > 1 && std::wstring(argv[1]) == L"--self-test") {
        return runSelfTest() ? 0 : 1;
    }

    std::cout << "=== taskbar_diagnostic ===\n";
    std::cout << "This diagnostic is read-only: no Explorer/taskbar/work-area modifications are performed.\n\n";

    bool hadFailure = false;

    HWND taskbarWindow = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbarWindow == nullptr) {
        hadFailure = true;
        std::cout << "[ERROR] FindWindowW(Shell_TrayWnd) did not find the taskbar window. "
                  << win32Error(GetLastError()) << "\n";
    } else {
        std::cout << "[OK] Taskbar HWND located: 0x" << std::hex << reinterpret_cast<std::uintptr_t>(taskbarWindow)
                  << std::dec << "\n";
    }

    if (taskbarWindow != nullptr) {
        RECT taskbarRect{};
        if (GetWindowRect(taskbarWindow, &taskbarRect)) {
            std::cout << "[OK] Taskbar rectangle via GetWindowRect: " << rectToString(taskbarRect) << "\n";
        } else {
            hadFailure = true;
            std::cout << "[ERROR] GetWindowRect(taskbar) failed: " << win32Error(GetLastError()) << "\n";
        }
    }

    APPBARDATA appBarData{};
    appBarData.cbSize = sizeof(appBarData);
    appBarData.hWnd = taskbarWindow;
    if (taskbarWindow != nullptr && SHAppBarMessage(ABM_GETTASKBARPOS, &appBarData) != FALSE) {
        std::cout << "[OK] Taskbar position via SHAppBarMessage(ABM_GETTASKBARPOS): edge="
                  << edgeToString(appBarData.uEdge) << ", rect=" << rectToString(appBarData.rc) << "\n";
    } else {
        hadFailure = true;
        std::cout << "[ERROR] SHAppBarMessage(ABM_GETTASKBARPOS) failed";
        if (taskbarWindow == nullptr) {
            std::cout << " because taskbar HWND is unavailable.";
        }
        std::cout << "\n";
    }

    if (taskbarWindow != nullptr) {
        const UINT state = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &appBarData));
        const bool autoHide = (state & ABS_AUTOHIDE) != 0;
        const bool alwaysOnTop = (state & ABS_ALWAYSONTOP) != 0;
        std::cout << "[INFO] Taskbar state flags: autoHide=" << (autoHide ? "true" : "false")
                  << ", alwaysOnTop=" << (alwaysOnTop ? "true" : "false") << "\n";
    }

    HMONITOR monitor = MonitorFromWindow(taskbarWindow != nullptr ? taskbarWindow : GetDesktopWindow(),
                                         MONITOR_DEFAULTTOPRIMARY);
    if (monitor == nullptr) {
        hadFailure = true;
        std::cout << "[ERROR] MonitorFromWindow failed: " << win32Error(GetLastError()) << "\n";
    } else {
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(monitor, &monitorInfo)) {
            std::cout << "[OK] Monitor device: " << toUtf8(monitorInfo.szDevice) << "\n";
            std::cout << "[OK] Monitor rectangle: " << rectToString(monitorInfo.rcMonitor) << "\n";
            std::cout << "[OK] Monitor work area: " << rectToString(monitorInfo.rcWork) << "\n";
            std::cout << "[INFO] Primary monitor: "
                      << ((monitorInfo.dwFlags & MONITORINFOF_PRIMARY) ? "true" : "false") << "\n";
        } else {
            hadFailure = true;
            std::cout << "[ERROR] GetMonitorInfoW failed: " << win32Error(GetLastError()) << "\n";
        }
    }

    const DpiResult dpiResult = detectDpi(taskbarWindow, monitor);
    if (dpiResult.ok) {
        const double scaleX = 100.0 * static_cast<double>(dpiResult.dpiX) / 96.0;
        const double scaleY = 100.0 * static_cast<double>(dpiResult.dpiY) / 96.0;

        std::cout << "[OK] DPI source: " << dpiResult.source << "\n";
        std::cout << "[OK] DPI: x=" << dpiResult.dpiX << ", y=" << dpiResult.dpiY << "\n";
        std::cout << std::fixed << std::setprecision(2) << "[OK] Scale factor: x=" << scaleX << "%, y=" << scaleY
                  << "%\n";
    } else {
        hadFailure = true;
        std::cout << "[ERROR] DPI detection failed: " << dpiResult.error << "\n";
    }

    std::cout << "\n[INFO] Coordinate model: Win32 rectangles above are physical pixels in the virtual-screen "
                 "coordinate space.\n";
    std::cout << "[INFO] Future Qt/QML overlay phases must convert DIP<->physical using per-monitor DPI.\n";

    if (hadFailure) {
        std::cout << "\nRESULT: FAIL (one or more required probes failed).\n";
        return 2;
    }

    std::cout << "\nRESULT: PASS\n";
    return 0;
}
