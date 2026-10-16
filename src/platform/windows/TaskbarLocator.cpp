#include "platform/windows/TaskbarLocator.h"

#include <shellapi.h>
#include <dwmapi.h>

#include <algorithm>
#include <cwchar>
#include <sstream>
#include <utility>

namespace w11widget::windows {
namespace {

Rect fromNative(const RECT& rectangle) {
    return {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
}

bool equal(const Rect& first, const Rect& second) {
    return first.left == second.left && first.top == second.top &&
           first.right == second.right && first.bottom == second.bottom;
}

bool contains(const Rect& outer, const Rect& inner) {
    return outer.isValid() && inner.isValid() && inner.left >= outer.left &&
           inner.top >= outer.top && inner.right <= outer.right && inner.bottom <= outer.bottom;
}

std::string failureMessage(const char* operation, DWORD error) {
    return std::string(operation) + " failed (Win32 error " + std::to_string(error) + ").";
}

std::string rectText(const Rect& rectangle) {
    return "{left=" + std::to_string(rectangle.left) + ", top=" + std::to_string(rectangle.top) +
           ", right=" + std::to_string(rectangle.right) + ", bottom=" +
           std::to_string(rectangle.bottom) + ", width=" + std::to_string(rectangle.width()) +
           ", height=" + std::to_string(rectangle.height()) + "}";
}

const char* edgeText(unsigned edge) {
    switch (edge) {
    case ABE_LEFT: return "left";
    case ABE_TOP: return "top";
    case ABE_RIGHT: return "right";
    case ABE_BOTTOM: return "bottom";
    default: return "unknown";
    }
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int count = static_cast<int>(value.size());
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), count,
                                         nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        return "[device name conversion failed]";
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), count,
                            result.data(), size, nullptr, nullptr) == 0) {
        return "[device name conversion failed]";
    }
    return result;
}

bool isDesktopOrTaskbar(HWND window) {
    if (window == GetDesktopWindow() || window == GetShellWindow()) {
        return true;
    }
    wchar_t name[128]{};
    if (GetClassNameW(window, name, 128) == 0) {
        return false;
    }
    const std::wstring className(name);
    return className == L"Progman" || className == L"WorkerW" ||
           className == L"Shell_TrayWnd" || className == L"Shell_SecondaryTrayWnd";
}

struct ObservedMonitor {
    HMONITOR handle = nullptr;
    MonitorDescription description;
};

struct MonitorEnumeration {
    std::vector<ObservedMonitor> monitors;
    const char* failure = nullptr;
    DWORD error = ERROR_SUCCESS;
};

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto& enumeration = *reinterpret_cast<MonitorEnumeration*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        enumeration.failure = "GetMonitorInfoW(enumerated monitor)";
        enumeration.error = GetLastError();
        return FALSE;
    }
    const Rect bounds = fromNative(info.rcMonitor);
    if (!bounds.isValid() || info.szDevice[0] == L'\0') {
        enumeration.failure = "Monitor enumeration returned invalid bounds or an empty device name.";
        return FALSE;
    }
    try {
        enumeration.monitors.push_back({monitor,
            {info.szDevice, bounds, (info.dwFlags & MONITORINFOF_PRIMARY) != 0}});
    } catch (...) {
        // Never unwind a C++ exception through the Win32 enumeration callback.
        enumeration.failure = "Monitor enumeration could not store a result.";
        return FALSE;
    }
    return TRUE;
}

std::vector<ObservedMonitor> readMonitors(std::string& error) {
    error.clear();
    MonitorEnumeration enumeration;
    SetLastError(ERROR_SUCCESS);
    if (!EnumDisplayMonitors(nullptr, nullptr, collectMonitor,
                             reinterpret_cast<LPARAM>(&enumeration))) {
        if (enumeration.failure != nullptr) {
            error = enumeration.error != ERROR_SUCCESS
                ? failureMessage(enumeration.failure, enumeration.error)
                : enumeration.failure;
        } else {
            error = failureMessage("EnumDisplayMonitors", GetLastError());
        }
        return {};
    }
    if (enumeration.monitors.empty()) {
        error = "No monitors were reported in this desktop session.";
        return {};
    }
    const auto primaryCount = std::count_if(enumeration.monitors.begin(),
                                           enumeration.monitors.end(),
                                           [](const ObservedMonitor& monitor) {
        return monitor.description.primary;
    });
    if (primaryCount != 1) {
        error = "Monitor enumeration did not report exactly one primary monitor.";
        return {};
    }
    for (std::size_t first = 0; first < enumeration.monitors.size(); ++first) {
        for (std::size_t second = first + 1; second < enumeration.monitors.size(); ++second) {
            if (_wcsicmp(enumeration.monitors[first].description.device.c_str(),
                         enumeration.monitors[second].description.device.c_str()) == 0) {
                error = "Monitor enumeration reported an ambiguous duplicate device name.";
                return {};
            }
        }
    }
    std::sort(enumeration.monitors.begin(), enumeration.monitors.end(),
              [](const ObservedMonitor& a, const ObservedMonitor& b) {
        if (a.description.primary != b.description.primary) {
            return a.description.primary;
        }
        return a.description.device < b.description.device;
    });
    return std::move(enumeration.monitors);
}

bool readMonitorSnapshot(TaskbarSnapshot& snapshot, std::string& error) {
    const HMONITOR monitor = MonitorFromWindow(snapshot.hwnd, MONITOR_DEFAULTTONULL);
    if (monitor == nullptr) {
        error = "The taskbar could not be associated with a monitor.";
        return false;
    }
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        error = failureMessage("GetMonitorInfoW(taskbar monitor)", GetLastError());
        return false;
    }
    snapshot.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    snapshot.monitorBounds = fromNative(info.rcMonitor);
    snapshot.workArea = fromNative(info.rcWork);
    snapshot.monitorDevice = info.szDevice;
    if (snapshot.monitorDevice.empty() || !contains(snapshot.monitorBounds, snapshot.bounds)
        || !contains(snapshot.monitorBounds, snapshot.workArea)) {
        error = "Taskbar or work-area geometry is invalid or outside its observed monitor.";
        return false;
    }
    return true;
}

bool readTaskbarDpi(TaskbarSnapshot& snapshot, std::string& error) {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
    const auto getDpiForWindow = user32 == nullptr ? nullptr :
        reinterpret_cast<GetDpiForWindowFunction>(GetProcAddress(user32, "GetDpiForWindow"));
    if (getDpiForWindow == nullptr) {
        error = "GetDpiForWindow is unavailable; no guessed DPI fallback is used for placement.";
        return false;
    }
    snapshot.dpi = getDpiForWindow(snapshot.hwnd);
    if (snapshot.dpi == 0) {
        error = "GetDpiForWindow(taskbar) returned zero DPI.";
        return false;
    }
    return true;
}

struct SecondaryTaskbarSearch {
    HMONITOR monitor = nullptr;
    HWND window = nullptr;
    bool ambiguous = false;
};

BOOL CALLBACK collectSecondaryTaskbar(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<SecondaryTaskbarSearch*>(data);
    wchar_t name[128]{};
    if (GetClassNameW(window, name, 128) == 0
        || wcscmp(name, L"Shell_SecondaryTrayWnd") != 0
        || MonitorFromWindow(window, MONITOR_DEFAULTTONULL) != search.monitor) {
        return TRUE;
    }
    if (search.window != nullptr) {
        search.ambiguous = true;
    } else {
        search.window = window;
    }
    return TRUE;
}

using DwmGetWindowAttributeFunction = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);

class DwmReadOnlyQuery {
public:
    DwmReadOnlyQuery() {
        // Load only the Windows system DLL; no additional runtime dependency.
        module_ = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module_ != nullptr) {
            query_ = reinterpret_cast<DwmGetWindowAttributeFunction>(
                GetProcAddress(module_, "DwmGetWindowAttribute"));
        }
    }
    ~DwmReadOnlyQuery() {
        if (module_ != nullptr) FreeLibrary(module_);
    }
    DwmReadOnlyQuery(const DwmReadOnlyQuery&) = delete;
    DwmReadOnlyQuery& operator=(const DwmReadOnlyQuery&) = delete;
    bool read(HWND window, DWORD attribute, void* result, DWORD size) const {
        return query_ != nullptr && SUCCEEDED(query_(window, attribute, result, size));
    }
private:
    HMODULE module_ = nullptr;
    DwmGetWindowAttributeFunction query_ = nullptr;
};

} // namespace

std::vector<MonitorDescription> enumerateMonitors(std::string& error) {
    const auto observed = readMonitors(error);
    std::vector<MonitorDescription> descriptions;
    descriptions.reserve(observed.size());
    for (const auto& monitor : observed) descriptions.push_back(monitor.description);
    return descriptions;
}

std::optional<unsigned> observedHorizontalEdge(const Rect& taskbar, const Rect& monitorBounds) {
    if (!contains(monitorBounds, taskbar) || taskbar.width() <= taskbar.height()
        || taskbar.left != monitorBounds.left || taskbar.right != monitorBounds.right) {
        return std::nullopt;
    }
    const bool top = taskbar.top == monitorBounds.top;
    const bool bottom = taskbar.bottom == monitorBounds.bottom;
    if (top == bottom) return std::nullopt;
    return top ? ABE_TOP : ABE_BOTTOM;
}

std::optional<TaskbarSnapshot> readTaskbar(std::string& error) {
    error.clear();
    TaskbarSnapshot snapshot;
    snapshot.hwnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (snapshot.hwnd == nullptr) {
        error = "Shell_TrayWnd is unavailable in this desktop session.";
        return std::nullopt;
    }

    RECT nativeBounds{};
    if (!GetWindowRect(snapshot.hwnd, &nativeBounds)) {
        error = failureMessage("GetWindowRect(taskbar)", GetLastError());
        return std::nullopt;
    }
    snapshot.bounds = fromNative(nativeBounds);
    if (!snapshot.bounds.isValid()) {
        error = "GetWindowRect(taskbar) returned an empty or inverted rectangle.";
        return std::nullopt;
    }

    APPBARDATA appbar{};
    appbar.cbSize = sizeof(appbar);
    appbar.hWnd = snapshot.hwnd;
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &appbar) == FALSE) {
        // This API does not document an extended GetLastError value.
        error = "SHAppBarMessage(ABM_GETTASKBARPOS) did not return taskbar geometry.";
        return std::nullopt;
    }
    if (!equal(snapshot.bounds, fromNative(appbar.rc))) {
        error = "GetWindowRect and ABM_GETTASKBARPOS disagree; placement is suspended.";
        return std::nullopt;
    }
    if (appbar.uEdge > ABE_BOTTOM) {
        error = "ABM_GETTASKBARPOS returned an unknown taskbar edge.";
        return std::nullopt;
    }
    snapshot.edge = appbar.uEdge;
    // Zero is a valid state (neither auto-hide nor always-on-top); GETSTATE has
    // no separate failure return, so do not interpret zero as an API failure.
    const auto state = SHAppBarMessage(ABM_GETSTATE, &appbar);
    snapshot.autoHide = (state & ABS_AUTOHIDE) != 0;

    if (!readMonitorSnapshot(snapshot, error)) {
        return std::nullopt;
    }
    if (!snapshot.primary) {
        error = "Shell_TrayWnd is not associated with the observed primary monitor.";
        return std::nullopt;
    }
    if (!readTaskbarDpi(snapshot, error)) {
        return std::nullopt;
    }
    return snapshot;
}

std::optional<TaskbarSnapshot> readTaskbarForMonitor(const std::wstring& device,
                                                    std::string& error) {
    if (device == L"primary") return readTaskbar(error);
    error.clear();
    if (device.empty()) {
        error = "A monitor device must be selected explicitly.";
        return std::nullopt;
    }
    const auto monitors = readMonitors(error);
    if (!error.empty()) return std::nullopt;
    const auto selected = std::find_if(monitors.begin(), monitors.end(),
                                     [&](const ObservedMonitor& monitor) {
        return _wcsicmp(monitor.description.device.c_str(), device.c_str()) == 0;
    });
    if (selected == monitors.end()) {
        error = "The selected monitor is no longer present: " + utf8(device) + ".";
        return std::nullopt;
    }
    if (selected->description.primary) {
        auto snapshot = readTaskbar(error);
        if (snapshot && _wcsicmp(snapshot->monitorDevice.c_str(), device.c_str()) != 0) {
            error = "The primary monitor changed while its taskbar was being probed.";
            return std::nullopt;
        }
        return snapshot;
    }

    SecondaryTaskbarSearch search;
    search.monitor = selected->handle;
    SetLastError(ERROR_SUCCESS);
    if (!EnumWindows(collectSecondaryTaskbar, reinterpret_cast<LPARAM>(&search))) {
        error = failureMessage("EnumWindows(secondary taskbars)", GetLastError());
        return std::nullopt;
    }
    if (search.window == nullptr || search.ambiguous) {
        error = search.ambiguous
            ? "More than one secondary taskbar was observed on the selected monitor."
            : "Shell_SecondaryTrayWnd is unavailable on the selected monitor. "
              "Windows may not display a taskbar there.";
        return std::nullopt;
    }
    TaskbarSnapshot snapshot;
    snapshot.hwnd = search.window;
    RECT bounds{};
    if (!GetWindowRect(snapshot.hwnd, &bounds)) {
        error = failureMessage("GetWindowRect(secondary taskbar)", GetLastError());
        return std::nullopt;
    }
    snapshot.bounds = fromNative(bounds);
    if (!readMonitorSnapshot(snapshot, error)) return std::nullopt;
    if (snapshot.primary || _wcsicmp(snapshot.monitorDevice.c_str(), device.c_str()) != 0
        || !equal(snapshot.monitorBounds, selected->description.bounds)) {
        error = "The selected secondary monitor changed during taskbar discovery.";
        return std::nullopt;
    }
    // ABM_GETTASKBARPOS ignores hWnd and reports the system taskbar, so never
    // reuse its primary-monitor rectangle for Shell_SecondaryTrayWnd.
    const auto edge = observedHorizontalEdge(snapshot.bounds, snapshot.monitorBounds);
    if (!edge) {
        error = "The observed secondary taskbar is not a full-width horizontal window "
                "on exactly one monitor edge.";
        return std::nullopt;
    }
    snapshot.edge = *edge;
    const HWND primaryTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (primaryTaskbar == nullptr) {
        error = "The primary taskbar is unavailable; global auto-hide state cannot be probed.";
        return std::nullopt;
    }
    APPBARDATA appbar{};
    appbar.cbSize = sizeof(appbar);
    appbar.hWnd = primaryTaskbar;
    // This is the shell's global taskbar state; zero is a valid response.
    snapshot.autoHide = (SHAppBarMessage(ABM_GETSTATE, &appbar) & ABS_AUTOHIDE) != 0;
    if (!readTaskbarDpi(snapshot, error)) return std::nullopt;
    return snapshot;
}

std::string describe(const TaskbarSnapshot& snapshot) {
    std::ostringstream output;
    output << "Taskbar HWND: 0x" << std::hex << reinterpret_cast<std::uintptr_t>(snapshot.hwnd)
           << std::dec << '\n'
           << "Taskbar: " << rectText(snapshot.bounds) << '\n'
           << "Edge: " << edgeText(snapshot.edge) << '\n'
           << "Auto-hide: " << (snapshot.autoHide ? "enabled" : "disabled") << '\n'
           << "Monitor device: " << utf8(snapshot.monitorDevice) << '\n'
           << "Primary monitor: " << (snapshot.primary ? "yes" : "no") << '\n'
           << "Geometry source: " << (snapshot.primary ? "GetWindowRect + ABM_GETTASKBARPOS"
                                                        : "observed secondary taskbar HWND") << '\n'
           << "Monitor: " << rectText(snapshot.monitorBounds) << '\n'
           << "Work area: " << rectText(snapshot.workArea) << '\n'
           << "DPI (GetDpiForWindow): " << snapshot.dpi << '\n'
           << "Coordinates: physical pixels in the virtual-screen coordinate space.\n"
           << "Taskbar geometry does not prove a region is unoccupied.";
    return output.str();
}

bool hasFullscreenConflict(const TaskbarSnapshot& snapshot, HWND ownWindow) {
    if (!snapshot.monitorBounds.isValid()) {
        return true;
    }
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || foreground == ownWindow || foreground == snapshot.hwnd ||
        !IsWindow(foreground) || !IsWindowVisible(foreground) ||
        isDesktopOrTaskbar(foreground) || IsIconic(foreground)) {
        return false;
    }
    DWORD foregroundProcess = 0;
    if (GetWindowThreadProcessId(foreground, &foregroundProcess) != 0
        && foregroundProcess == GetCurrentProcessId()) {
        return false;
    }
    if (ownWindow != nullptr && GetAncestor(foreground, GA_ROOTOWNER) == ownWindow) {
        return false;
    }
    static const DwmReadOnlyQuery dwm;
    DWORD cloaked = 0;
    if (dwm.read(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) && cloaked != 0) {
        return false;
    }
    RECT bounds{};
    if (!dwm.read(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))
        || !fromNative(bounds).isValid()) {
        if (!GetWindowRect(foreground, &bounds)) return true;
    }
    if (!fromNative(bounds).isValid()) {
        return true;
    }
    // A foreground window covering the target monitor is a conflict, including
    // borderless fullscreen and windows extending over more than one monitor.
    // Maximized windows normally stop at the work area and therefore pass.
    return contains(fromNative(bounds), snapshot.monitorBounds);
}

} // namespace w11widget::windows
