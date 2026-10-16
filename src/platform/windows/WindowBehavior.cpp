#include "WindowBehavior.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#include <limits>
#include <map>
#include <utility>

namespace w11widget::windows {
namespace {

struct RegisteredTaskbarMessage {
    UINT identifier = 0;
    DWORD error = ERROR_SUCCESS;
};

const RegisteredTaskbarMessage& taskbarCreatedMessage()
{
    static const RegisteredTaskbarMessage message = []() {
        const UINT identifier = RegisterWindowMessageW(L"TaskbarCreated");
        return RegisteredTaskbarMessage{
            identifier, identifier == 0 ? GetLastError() : ERROR_SUCCESS};
    }();
    return message;
}

QString apiError(const QString& operation, DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    QString detail;
    if (length != 0 && buffer != nullptr) {
        detail = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return QStringLiteral("%1 failed (Win32 %2)%3")
        .arg(operation).arg(code)
        .arg(detail.isEmpty() ? QString() : QStringLiteral(": ") + detail);
}

QString rectDescription(const Rect& rect)
{
    return QStringLiteral("{%1, %2, %3, %4}")
        .arg(rect.left).arg(rect.top).arg(rect.right).arg(rect.bottom);
}

bool hasOverlappingWindowAbove(HWND window, const RECT& bounds)
{
    // TOPMOST alone is insufficient: the taskbar is also a topmost window.
    // Read the actual order, ignoring hidden/minimized and non-overlapping windows.
    unsigned examined = 0;
    for (HWND above = GetWindow(window, GW_HWNDPREV); above != nullptr;
         above = GetWindow(above, GW_HWNDPREV)) {
        // Other processes can change the list while we read it. Never spin
        // indefinitely if the desktop order is changing continuously.
        if (++examined > 4096) return true;
        if (!IsWindowVisible(above) || IsIconic(above)) continue;
        RECT other{}, overlap{};
        if (GetWindowRect(above, &other) && IntersectRect(&overlap, &bounds, &other))
            return true;
    }
    return false;
}

} // namespace

struct TaskbarZOrderObserver::Impl {
    HWINEVENTHOOK hook = nullptr;
    HWND observed = nullptr;
    DWORD process = 0;
    DWORD thread = 0;
    bool delivering = false;
    std::function<void()> changed;

    static auto& observers()
    {
        // WinEvent callbacks run on the thread that registered the observer.
        static thread_local std::map<HWINEVENTHOOK, Impl*> instances;
        return instances;
    }

    static void CALLBACK event(HWINEVENTHOOK hook, DWORD eventId, HWND window,
                               LONG object, LONG, DWORD, DWORD)
    {
        const auto found = observers().find(hook);
        if (found == observers().end()) return;
        Impl& self = *found->second;
        if (self.delivering) return;
        // A top-level reorder is reported against the desktop container, not
        // the taskbar HWND. The emitting process is filtered by Windows.
        const bool reordered = eventId == EVENT_OBJECT_REORDER
            && window == GetDesktopWindow() && object == OBJID_CLIENT;
        const bool shown = eventId == EVENT_OBJECT_SHOW
            && window == self.observed && object == OBJID_WINDOW;
        if (!reordered && !shown) return;
        self.delivering = true;
        self.changed();
        self.delivering = false;
    }
};

TaskbarZOrderObserver::TaskbarZOrderObserver(std::function<void()> changed)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->changed = std::move(changed);
}

TaskbarZOrderObserver::~TaskbarZOrderObserver() { stop(); }

void TaskbarZOrderObserver::stop()
{
    if (m_impl->hook == nullptr) return;
    Impl::observers().erase(m_impl->hook);
    UnhookWinEvent(m_impl->hook);
    m_impl->hook = nullptr;
    m_impl->observed = nullptr;
}

bool TaskbarZOrderObserver::observe(WId taskbarWindow, QString& error)
{
    error.clear();
    const HWND window = reinterpret_cast<HWND>(taskbarWindow);
    DWORD process = 0;
    const DWORD thread = IsWindow(window) ? GetWindowThreadProcessId(window, &process) : 0;
    // Never fall back to a global subscription if a taskbar probe becomes stale.
    if (thread == 0 || process == 0) {
        error = QStringLiteral("The taskbar window is unavailable for z-order notifications.");
        return false;
    }
    if (m_impl->hook != nullptr && m_impl->observed == window
        && m_impl->thread == thread && m_impl->process == process) return true;
    stop();
    SetLastError(ERROR_SUCCESS);
    const HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_REORDER, nullptr, &Impl::event,
        process, 0, WINEVENT_OUTOFCONTEXT);
    if (hook == nullptr) {
        error = apiError(QStringLiteral("SetWinEventHook(taskbar process)"), GetLastError());
        return false;
    }
    m_impl->hook = hook;
    m_impl->observed = window;
    m_impl->process = process;
    m_impl->thread = thread;
    Impl::observers().emplace(hook, m_impl.get());
    return true;
}

bool preparePanelWindow(WId nativeWindow, QString& error)
{
    error.clear();
    const HWND window = reinterpret_cast<HWND>(nativeWindow);
    if (window == nullptr || !IsWindow(window)) {
        error = QStringLiteral("The panel native window is unavailable.");
        return false;
    }
    const RegisteredTaskbarMessage& shellRestartMessage = taskbarCreatedMessage();
    if (shellRestartMessage.identifier == 0) {
        error = apiError(QStringLiteral("RegisterWindowMessage(TaskbarCreated)"),
                         shellRestartMessage.error);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR owner = GetWindowLongPtrW(window, GWLP_HWNDPARENT);
    DWORD code = GetLastError();
    if (owner == 0 && code != ERROR_SUCCESS) {
        error = apiError(QStringLiteral("GetWindowLongPtr(GWLP_HWNDPARENT)"), code);
        return false;
    }
    if (owner != 0) {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previousOwner = SetWindowLongPtrW(window, GWLP_HWNDPARENT, 0);
        code = GetLastError();
        if (previousOwner == 0 && code != ERROR_SUCCESS) {
            error = apiError(QStringLiteral("SetWindowLongPtr(GWLP_HWNDPARENT)"), code);
            return false;
        }
    }

    // Zero can be a successful style value; inspect the last error explicitly.
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR originalStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    code = GetLastError();
    if (originalStyle == 0 && code != ERROR_SUCCESS) {
        error = apiError(QStringLiteral("GetWindowLongPtr(GWL_EXSTYLE)"), code);
        return false;
    }
    const LONG_PTR desiredStyle =
        (originalStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
    if (desiredStyle == originalStyle) return true;
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousStyle = SetWindowLongPtrW(window, GWL_EXSTYLE, desiredStyle);
    code = GetLastError();
    if (previousStyle == 0 && code != ERROR_SUCCESS) {
        error = apiError(QStringLiteral("SetWindowLongPtr(GWL_EXSTYLE)"), code);
        return false;
    }
    if (!SetWindowPos(window, nullptr, 0, 0, 0, 0,
                      SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE
                          | SWP_NOZORDER | SWP_NOACTIVATE)) {
        error = apiError(QStringLiteral("SetWindowPos(SWP_FRAMECHANGED)"),
                         GetLastError());
        return false;
    }
    return true;
}

bool protectPanelComposition(WId nativeWindow, QString& error)
{
    error.clear();
    const HWND window = reinterpret_cast<HWND>(nativeWindow);
    DWORD process = 0;
    if (!IsWindow(window) || GetWindowThreadProcessId(window, &process) == 0
        || process != GetCurrentProcessId()) {
        error = QStringLiteral("DWM protection requires this application's own panel window.");
        return false;
    }
    // Peek fades a composed window without changing WS_VISIBLE, TOPMOST or
    // exposure. Prevent that effect at the compositor instead of restoring it
    // after a native visibility/z-order notification that might never arrive.
    const BOOL enabled = TRUE;
    for (const auto attribute : {DWMWA_EXCLUDED_FROM_PEEK, DWMWA_TRANSITIONS_FORCEDISABLED}) {
        const HRESULT result = DwmSetWindowAttribute(window, attribute, &enabled, sizeof(enabled));
        if (FAILED(result)) {
            const QString operation = attribute == DWMWA_EXCLUDED_FROM_PEEK
                ? QStringLiteral("DwmSetWindowAttribute(EXCLUDED_FROM_PEEK)")
                : QStringLiteral("DwmSetWindowAttribute(TRANSITIONS_FORCEDISABLED)");
            error = QStringLiteral("%1 failed (HRESULT 0x%2).")
                .arg(operation).arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
            return false;
        }
    }
    return true;
}

bool placePanelWindow(WId nativeWindow, const Rect& requestedBounds,
                      bool showWindow, Rect& actualBounds, QString& error)
{
    error.clear();
    const qint64 width = static_cast<qint64>(requestedBounds.right) - requestedBounds.left;
    const qint64 height = static_cast<qint64>(requestedBounds.bottom) - requestedBounds.top;
    if (width <= 0 || height <= 0
        || width > (std::numeric_limits<int>::max)()
        || height > (std::numeric_limits<int>::max)()) {
        error = QStringLiteral("The requested physical panel rectangle is invalid.");
        return false;
    }
    const HWND window = reinterpret_cast<HWND>(nativeWindow);
    RECT before{};
    if (!GetWindowRect(window, &before)) {
        error = apiError(QStringLiteral("GetWindowRect(panel)"), GetLastError());
        return false;
    }
    if (showWindow && IsIconic(window)) {
        // Restoring only our own window must not activate it.
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    UINT flags = SWP_NOACTIVATE;
    if (before.left == requestedBounds.left && before.top == requestedBounds.top)
        flags |= SWP_NOMOVE;
    if (static_cast<qint64>(before.right) - before.left == width
        && static_cast<qint64>(before.bottom) - before.top == height)
        flags |= SWP_NOSIZE;
    if (showWindow && !IsWindowVisible(window)) flags |= SWP_SHOWWINDOW;
    const RECT requested{requestedBounds.left, requestedBounds.top,
                         requestedBounds.right, requestedBounds.bottom};
    if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0
        && !hasOverlappingWindowAbove(window, requested)) flags |= SWP_NOZORDER;
    const UINT unchanged = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
    if ((flags & unchanged) != unchanged || (flags & SWP_SHOWWINDOW) != 0) {
        if (!SetWindowPos(window, HWND_TOPMOST, requestedBounds.left, requestedBounds.top,
                          static_cast<int>(width), static_cast<int>(height), flags)) {
            error = apiError(QStringLiteral("SetWindowPos(panel)"), GetLastError());
            return false;
        }
    }
    RECT actual{};
    if (!GetWindowRect(window, &actual)) {
        error = apiError(QStringLiteral("GetWindowRect(panel)"), GetLastError());
        return false;
    }
    actualBounds = {actual.left, actual.top, actual.right, actual.bottom};
    if (actualBounds.left != requestedBounds.left
        || actualBounds.top != requestedBounds.top
        || actualBounds.right != requestedBounds.right
        || actualBounds.bottom != requestedBounds.bottom) {
        error = QStringLiteral("Panel geometry differs from the approved physical rectangle: "
                               "requested %1, actual %2.")
            .arg(rectDescription(requestedBounds), rectDescription(actualBounds));
        return false;
    }
    return true;
}

PanelNativeMessage inspectPanelNativeMessage(void* nativeMessage, bool keepVisible)
{
    if (nativeMessage == nullptr) {
        return {};
    }
    const auto* message = static_cast<const MSG*>(nativeMessage);
    if (message->message == WM_DWMCOMPOSITIONCHANGED) {
        return {false, false, 0, 0, false, false, true};
    }
    if (keepVisible) {
        if (message->message == WM_WINDOWPOSCHANGING && message->lParam != 0) {
            auto* position = reinterpret_cast<WINDOWPOS*>(message->lParam);
            // Modify the pending operation before Windows applies it, rather
            // than allowing a hide/demotion and showing the panel on a timer.
            position->flags &= ~SWP_HIDEWINDOW;
            if ((position->flags & SWP_NOZORDER) == 0)
                position->hwndInsertAfter = HWND_TOPMOST;
        }
        if (message->message == WM_SHOWWINDOW && message->wParam == FALSE) {
            // Do not let a rejected native hide suspend the Qt Quick scene.
            return {true, false, 0};
        }
        if (message->message == WM_SYSCOMMAND
            && (message->wParam & 0xfff0U) == SC_MINIMIZE) {
            return {true, false, 0};
        }
        if (message->message == WM_ACTIVATEAPP || message->message == WM_ACTIVATE) {
            return {false, false, 0, 0, false, true};
        }
    }
    if (message->message == WM_MOUSEACTIVATE) {
        return {true, false, MA_NOACTIVATE};
    }
    if (message->message == WM_DPICHANGED) {
        // Let Qt update its DPI/screen state without changing visibility policy.
        return {false, true, 0};
    }
    if (message->message == WM_TIMECHANGE) {
        PanelNativeMessage result;
        result.clockChanged = true;
        return result;
    }
    const UINT shellRestartMessage = taskbarCreatedMessage().identifier;
    if (message->message == WM_POWERBROADCAST
        && (message->wParam == PBT_APMSUSPEND
            || message->wParam == PBT_APMRESUMEAUTOMATIC
            || message->wParam == PBT_APMRESUMESUSPEND)) {
        // Notify, then let Qt handle the power notification normally.
        return {false, true, 0, message->wParam == PBT_APMSUSPEND ? 1 : 2};
    }
    if (shellRestartMessage != 0 && message->message == shellRestartMessage) {
        return {false, true, 0, 0, true};
    }
    if (message->message == WM_DISPLAYCHANGE || message->message == WM_SETTINGCHANGE
        || message->message == WM_DEVICECHANGE) {
        return {false, true, 0};
    }
    return {};
}

} // namespace w11widget::windows
