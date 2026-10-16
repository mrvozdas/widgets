#include "ui/PanelWindow.h"
#include "platform/windows/WindowBehavior.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QEvent>
#include <QImage>
#include <QQuickWindow>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <iostream>

namespace {

struct OwnedProbeWindow {
    HWND handle = nullptr;
    ~OwnedProbeWindow() {
        if (handle != nullptr) DestroyWindow(handle);
    }
};

struct WindowMessages {
    unsigned positions = 0;
    unsigned frameChanges = 0;
    unsigned styles = 0;
    unsigned hiddenPositions = 0;
    unsigned hiddenNotifications = 0;
    unsigned minimizedSizes = 0;
    unsigned nativeVisibilityLosses = 0;
    unsigned qtHiddenTransitions = 0;
    unsigned emptyExposes = 0;
};

class OwnWindowObserver final : public QObject {
public:
    explicit OwnWindowObserver(QWindow& observed)
        : observed_(observed), window_(reinterpret_cast<HWND>(observed.winId())) {
        previous_ = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window_, GWLP_WNDPROC));
        if (!previous_ || !SetPropW(window_, propertyKey(), this)) return;
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR old = SetWindowLongPtrW(
            window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc));
        if (old == 0 && GetLastError() != ERROR_SUCCESS) {
            RemovePropW(window_, propertyKey());
            return;
        }
        if (old != 0) previous_ = reinterpret_cast<WNDPROC>(old);
        installed_ = true;
        observed_.installEventFilter(this);
        connect(&observed_, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible) { ++messages.qtHiddenTransitions; ++total.qtHiddenTransitions; }
        });
    }

    ~OwnWindowObserver() override {
        observed_.removeEventFilter(this);
        if (installed_ && IsWindow(window_)) {
            SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous_));
            RemovePropW(window_, propertyKey());
        }
    }

    OwnWindowObserver(const OwnWindowObserver&) = delete;
    OwnWindowObserver& operator=(const OwnWindowObserver&) = delete;
    bool installed() const noexcept { return installed_; }
    void reset() { messages = {}; }
    void resetAll() { messages = {}; total = {}; }
    WindowMessages messages;
    WindowMessages total;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == &observed_ && event->type() == QEvent::Expose && !observed_.isExposed()) {
            ++messages.emptyExposes;
            ++total.emptyExposes;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static LPCWSTR propertyKey() noexcept { return L"w11widget.test.own-window-observer"; }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM value, LPARAM parameter) {
        auto* observer = static_cast<OwnWindowObserver*>(GetPropW(window, propertyKey()));
        if (!observer) return DefWindowProcW(window, message, value, parameter);
        const bool visibleBefore = IsWindowVisible(window) != FALSE;
        const LRESULT result = CallWindowProcW(observer->previous_, window, message, value, parameter);
        // The original Qt/native handler has now applied any protection to
        // WINDOWPOS. Measure the final flags, rather than the attempted flags.
        const bool visibilityLost = visibleBefore && !IsWindowVisible(window);
        const auto record = [&](WindowMessages& counts) {
            if (message == WM_WINDOWPOSCHANGING && parameter != 0) {
                const auto* position = reinterpret_cast<const WINDOWPOS*>(parameter);
                ++counts.positions;
                if ((position->flags & SWP_FRAMECHANGED) != 0) ++counts.frameChanges;
            }
            if (message == WM_STYLECHANGING || message == WM_STYLECHANGED) ++counts.styles;
            if (message == WM_WINDOWPOSCHANGED && parameter != 0 &&
                (reinterpret_cast<const WINDOWPOS*>(parameter)->flags & SWP_HIDEWINDOW) != 0) {
                ++counts.hiddenPositions;
            }
            if (message == WM_SHOWWINDOW && value == FALSE) ++counts.hiddenNotifications;
            if (message == WM_SIZE && value == SIZE_MINIMIZED) ++counts.minimizedSizes;
            if (visibilityLost) ++counts.nativeVisibilityLosses;
        };
        record(observer->messages);
        record(observer->total);
        return result;
    }

    QWindow& observed_;
    HWND window_ = nullptr;
    WNDPROC previous_ = nullptr;
    bool installed_ = false;
};

bool isAbove(HWND first, HWND second) {
    for (HWND window = GetWindow(second, GW_HWNDPREV); window != nullptr;
         window = GetWindow(window, GW_HWNDPREV)) {
        if (window == first) return true;
    }
    return false;
}

template <typename Check>
void checkHiddenGuardPolicy(Check&& check) {
    // These are fixture messages, never sent to a live window in normal CTest.
    WINDOWPOS position{nullptr, HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE};
    MSG message{};
    message.message = WM_WINDOWPOSCHANGING;
    message.lParam = reinterpret_cast<LPARAM>(&position);
    const auto pinned = w11widget::windows::inspectPanelNativeMessage(&message, true);
    check((position.flags & SWP_HIDEWINDOW) == 0, "Pinned policy removes an attempted native Hide");
    check(position.hwndInsertAfter == HWND_TOPMOST, "Pinned policy prevents native topmost demotion");
    check(!pinned.handled, "Protected WINDOWPOS still passes through normal native processing");

    position.hwndInsertAfter = HWND_NOTOPMOST;
    position.flags = SWP_HIDEWINDOW | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE;
    w11widget::windows::inspectPanelNativeMessage(&message, true);
    check((position.flags & SWP_HIDEWINDOW) == 0 && (position.flags & SWP_NOZORDER) != 0 &&
          position.hwndInsertAfter == HWND_NOTOPMOST,
          "Pinned no-z-order operation only removes Hide without introducing a reorder");

    position.hwndInsertAfter = HWND_NOTOPMOST;
    position.flags = SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE;
    const auto unpinned = w11widget::windows::inspectPanelNativeMessage(&message, false);
    check(!unpinned.handled && (position.flags & SWP_HIDEWINDOW) != 0 &&
          position.hwndInsertAfter == HWND_NOTOPMOST,
          "Unpinned policy permits explicit Hide and z-order changes");

    message.message = WM_SHOWWINDOW;
    message.wParam = FALSE;
    message.lParam = 0;
    check(w11widget::windows::inspectPanelNativeMessage(&message, true).handled,
          "Pinned native Hide notification cannot stop the Qt scene");
    check(!w11widget::windows::inspectPanelNativeMessage(&message, false).handled,
          "Unpinned Hide notification reaches normal Qt processing");
    message.wParam = TRUE;
    check(!w11widget::windows::inspectPanelNativeMessage(&message, true).handled,
          "Pinned Show notification still reaches normal Qt processing");

    message.message = WM_SYSCOMMAND;
    message.wParam = SC_MINIMIZE | 2U;
    check(w11widget::windows::inspectPanelNativeMessage(&message, true).handled,
          "Pinned policy consumes minimize system commands including their low bits");
    check(!w11widget::windows::inspectPanelNativeMessage(&message, false).handled,
          "Unpinned policy does not block minimize system commands");
    message.wParam = SC_RESTORE;
    check(!w11widget::windows::inspectPanelNativeMessage(&message, true).handled,
          "Pinned policy does not suppress unrelated system commands");

    message.message = WM_TIMECHANGE;
    const auto clockChange = w11widget::windows::inspectPanelNativeMessage(&message, true);
    check(clockChange.clockChanged && !clockChange.placementInvalidated,
          "Clock-change messages refresh widgets without invalidating panel placement");
}

template <typename Check>
void checkVisibleRetention(w11widget::PanelWindow& panel, Check&& check) {
    const HWND window = reinterpret_cast<HWND>(panel.winId());
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        check(false, "Visible regression can read the local work area");
        return;
    }
    const int left = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - 240) / 2;
    const int top = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - 32) / 2;
    const w11widget::Rect requested{left, top, left + 240, top + 32};
    OwnedProbeWindow first;
    OwnedProbeWindow second;
    first.handle = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Focus probe A", WS_POPUP | WS_BORDER,
                                  left, top - 100, 110, 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    second.handle = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Focus probe B", WS_POPUP | WS_BORDER,
                                   left + 130, top - 100, 110, 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (first.handle == nullptr || second.handle == nullptr) {
        check(false, "Focus regression creates only its own temporary windows");
        return;
    }

    QEventLoop loop;
    OwnWindowObserver observer(panel);
    if (!observer.installed()) {
        check(false, "Visible regression attaches an observer only to its own HWND");
        return;
    }
    QString error;
    const auto visible = [&]() {
        return panel.isVisible() && IsWindowVisible(window) != FALSE;
    };
    const auto pinnedVisible = [&]() {
        return visible() && !IsIconic(window) &&
               (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    };
    QTimer::singleShot(0, &loop, [&]() {
        check(panel.showAt(requested, error), "Own panel shows at work-area center");
        check(visible(), "Initial Show sets Qt and native visibility");
        observer.resetAll();
        ShowWindow(first.handle, SW_SHOWNORMAL);
        SetForegroundWindow(first.handle);
    });
    QTimer::singleShot(100, &loop, [&]() {
        check(visible(), "Activating another owned window keeps panel visible");
        ShowWindow(second.handle, SW_SHOWNORMAL);
        SetForegroundWindow(second.handle);
    });
    QTimer::singleShot(250, &loop, [&]() {
        check(visible(), "Switching between ordinary owned windows keeps panel visible");
        const HWND active = GetForegroundWindow();
        const bool ownedFocus = active == first.handle || active == second.handle;
        std::cout << (ownedFocus ? "[OK] Owned focus switch observed\n" :
                                  "[INFO] Windows denied foreground assignment; focus switch inconclusive\n");
        SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
        SendMessageW(window, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0),
                     reinterpret_cast<LPARAM>(second.handle));
        QCoreApplication::processEvents();
        check(visible(), "Application and window deactivation do not hide the panel");
        SendMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
        QCoreApplication::processEvents();
        check(visible(), "Application reactivation preserves panel visibility");
        SendMessageW(window, WM_SETTINGCHANGE, 0, 0);
        check(visible(), "Settings notification does not hide a shown panel");
        SendMessageW(window, WM_DWMCOMPOSITIONCHANGED, 0, 0);
        check(visible(), "Composition-policy refresh never hides a shown panel");
        SendMessageW(window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
        check(visible(), "Suspend notification does not hide a shown panel");
        SendMessageW(window, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
        check(visible(), "Resume notification does not hide a shown panel");
        const UINT shellMessage = RegisterWindowMessageW(L"TaskbarCreated");
        if (shellMessage != 0) SendMessageW(window, shellMessage, 0, 0);
        check(visible(), "Shell notification does not hide a shown panel");
        RECT current{};
        if (GetWindowRect(window, &current)) {
            const UINT dpi = GetDpiForWindow(window);
            if (dpi != 0) SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                                      reinterpret_cast<LPARAM>(&current));
        }
        check(visible(), "DPI notification does not hide a shown panel");
        check(!panel.keepVisibleAt({}, error), "Invalid retention geometry is reported");
        check(visible(), "Retention error leaves the shown panel visible");
    });
    QTimer::singleShot(375, &loop, [&]() {
        observer.reset();
        for (int attempt = 0; attempt < 10; ++attempt) {
            check(panel.keepVisibleAt(requested, error), "Stable retention succeeds without repairs");
            check(pinnedVisible(), "Stable retention leaves visibility and topmost state intact");
        }
        check(observer.messages.positions == 0, "Ten stable retentions send no WINDOWPOSCHANGING messages");
        check(observer.messages.styles == 0 && observer.messages.frameChanges == 0,
              "Ten stable retentions send no style changes or SWP_FRAMECHANGED");
        check(observer.messages.hiddenPositions == 0 && observer.messages.hiddenNotifications == 0 &&
              observer.messages.minimizedSizes == 0 && observer.messages.nativeVisibilityLosses == 0 &&
              observer.messages.qtHiddenTransitions == 0,
              "Stable retention produces no native or Qt hidden/minimized transitions");
    });
    QTimer::singleShot(450, &loop, [&]() {
        observer.reset();
        check(SetWindowPos(window, nullptr, 0, 0, 0, 0,
                           SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                           SWP_NOACTIVATE) != FALSE,
              "Own-window native Hide request reaches the visibility guard");
        check(pinnedVisible(), "SetWindowPos Hide is prevented immediately without a maintenance tick");
        ShowWindow(window, SW_HIDE);
        check(pinnedVisible(), "ShowWindow Hide is prevented immediately without event pumping");
        SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        check(pinnedVisible(), "System minimize is prevented before the native window becomes iconic");
        check(SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE,
              "Own-window topmost-demotion request reaches the visibility guard");
        check(pinnedVisible(), "Topmost demotion is prevented immediately without repair");
        check(observer.messages.positions != 0, "Native guard regression observed real WINDOWPOSCHANGING messages");
        check(observer.messages.hiddenPositions == 0 && observer.messages.minimizedSizes == 0 &&
              observer.messages.nativeVisibilityLosses == 0 && observer.messages.qtHiddenTransitions == 0,
              "Blocked native attempts never hide/minimize the HWND or Qt window");
        const QImage frame = panel.grabWindow();
        check(!frame.isNull() && frame.pixelColor(frame.width() / 2, frame.height() / 2).alpha() > 0,
              "Qt Quick still renders a nontransparent frame after blocked native Hide");
    });
    QTimer::singleShot(500, &loop, [&]() {
        check(SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING) != FALSE,
              "Guard-bypass probe lowers only its own panel to test fallback repair");
        check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
              "Fallback repair starts with a genuinely demoted own window");
        const HWND focusBefore = GetForegroundWindow();
        observer.reset();
        check(panel.keepVisibleAt(requested, error), "Retention restores approved topmost position");
        check(visible(), "Retention preserves Qt and native visibility");
        check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
              "Retention restores WS_EX_TOPMOST");
        check(GetForegroundWindow() == focusBefore, "Retention does not change owned focus");
        check(observer.messages.frameChanges == 0, "Topmost-only repair never sends SWP_FRAMECHANGED");
        observer.reset();
        check(panel.keepVisibleAt(requested, error), "Second retention is stable after fallback repair");
        check(observer.messages.positions == 0 && observer.messages.styles == 0 &&
              observer.messages.frameChanges == 0,
              "Stable call after repair performs no window-position or style mutations");
    });
    QTimer::singleShot(700, &loop, [&]() {
        check(SetWindowPos(second.handle, HWND_TOPMOST, requested.left, requested.top,
                           static_cast<int>(requested.width()), static_cast<int>(requested.height()),
                           SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE,
              "Unobserved owned surrogate can cover the panel's exact rectangle");
        check(!isAbove(window, second.handle),
              "Surrogate genuinely covers the panel before observer registration");
        check(panel.watchTaskbar(reinterpret_cast<WId>(second.handle), error),
              "Scoped reorder observer can watch an owned taskbar surrogate");
        check(isAbove(window, second.handle), "Observer registration immediately repairs existing overlap");
        observer.reset();
        check(SetWindowPos(second.handle, HWND_TOPMOST, requested.left, requested.top,
                           static_cast<int>(requested.width()), static_cast<int>(requested.height()),
                           SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE,
              "Owned surrogate is raised topmost over the panel's exact rectangle");
        check(visible(), "Surrogate overlap never changes native or Qt visibility");
        // A Win32 operation may itself dispatch the queued WinEvent callback
        // before returning. The unobserved preflight above proves the fixture
        // really covers; require a new panel repair for this second raise.
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        check(isAbove(window, second.handle), "Scoped native reorder notification raises the panel above its surrogate");
        check(observer.messages.positions > 0, "Reorder notification performs a real own-panel repair");
        check(pinnedVisible() && observer.messages.nativeVisibilityLosses == 0 &&
              observer.messages.qtHiddenTransitions == 0 && observer.messages.hiddenPositions == 0,
              "Asynchronous reorder repair never hides the panel or resets Qt visibility");
        observer.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        check(observer.messages.positions == 0 && observer.messages.frameChanges == 0,
              "Settled reorder observation does not cause repetitive z-order mutation");
        const QImage frame = panel.grabWindow();
        check(!frame.isNull() && frame.pixelColor(frame.width() / 2, frame.height() / 2).alpha() > 0,
              "Qt Quick retains a nontransparent frame after reorder recovery");
    });
    QTimer::singleShot(1000, &loop, [&]() {
        check(observer.total.nativeVisibilityLosses == 0 && observer.total.qtHiddenTransitions == 0 &&
              observer.total.minimizedSizes == 0 && observer.total.hiddenPositions == 0,
              "Entire pinned regression has no native hide/minimize or Qt hide transition");
        std::cout << "[TRACE] Empty expose events while pinned: " << observer.total.emptyExposes << '\n';
        panel.hide();
        check(!panel.isVisible() && !IsWindowVisible(window), "Explicit manual Hide still works");
        observer.reset();
        SetWindowPos(second.handle, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        check(!panel.isVisible() && !IsWindowVisible(window) && observer.messages.positions == 0,
              "Reorder events after manual Hide cannot show or reposition the panel");
        loop.quit();
    });
    loop.exec();
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QQuickWindow::setDefaultAlphaBuffer(true);

    int failures = 0;
    const auto check = [&](bool passed, const char* description) {
        if (passed) {
            std::cout << "[OK] " << description << '\n';
        } else {
            ++failures;
            std::cerr << "[FAIL] " << description << '\n';
        }
    };

    const HWND foregroundBefore = GetForegroundWindow();
    w11widget::PanelWindow panel;
    int environmentChanges = 0;
    int powerChanges = 0;
    bool suspended = false;
    int shellRecreations = 0;
    QObject::connect(&panel, &w11widget::PanelWindow::environmentChanged,
                     &application, [&]() { ++environmentChanges; });
    QObject::connect(&panel, &w11widget::PanelWindow::powerStateChanged,
                     &application, [&](bool state) { ++powerChanges; suspended = state; });
    QObject::connect(&panel, &w11widget::PanelWindow::shellRecreated,
                     &application, [&]() { ++shellRecreations; });

    QString error;
    if (!panel.initialize(error)) {
        std::cerr << "[FAIL] Hidden PanelWindow initialization: "
                  << error.toStdString() << '\n';
        return 1;
    }
    check(panel.status() == QQuickView::Ready && panel.rootObject() != nullptr,
          "Resource QML loaded synchronously");

    const HWND window = reinterpret_cast<HWND>(panel.winId());
    check(IsWindow(window) != FALSE, "Panel owns a valid native window");
    if (!IsWindow(window)) {
        return 1;
    }
    const auto checkHidden = [&]() {
        check(!panel.isVisible() && IsWindowVisible(window) == FALSE,
              "Qt and native window remain hidden");
    };
    checkHidden();
    checkHiddenGuardPolicy(check);
    checkHidden();

    check(!w11widget::windows::protectPanelComposition(0, error) && !error.isEmpty(),
          "Composition protection rejects an unavailable window with an explicit error");
    check(!w11widget::windows::protectPanelComposition(
              reinterpret_cast<WId>(GetDesktopWindow()), error) && !error.isEmpty(),
          "Composition protection rejects another process's desktop window");
    checkHidden();
    int compositionErrors = 0;
    const auto compositionConnection = QObject::connect(
        &panel, &w11widget::PanelWindow::visibilityMaintenanceFailed,
        &application, [&](const QString&) { ++compositionErrors; });
    SendMessageW(window, WM_DWMCOMPOSITIONCHANGED, 0, 0);
    check(compositionErrors == 0, "DWM composition notification reapplies protection successfully");
    QObject::disconnect(compositionConnection);
    checkHidden();

    check((panel.flags() & Qt::WindowType_Mask) == Qt::Window,
          "Qt window type is an independent ordinary window");
    check(panel.isTopLevel() && panel.parent() == nullptr && panel.transientParent() == nullptr,
          "Panel has no Qt parent or transient owner");
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR owner = GetWindowLongPtrW(window, GWLP_HWNDPARENT);
    const DWORD ownerError = GetLastError();
    check(owner == 0 && ownerError == ERROR_SUCCESS, "Panel HWND has no native owner");

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR styles = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const DWORD styleError = GetLastError();
    check(styles != 0 || styleError == ERROR_SUCCESS, "Native styles can be read");
    check((styles & WS_EX_NOACTIVATE) != 0, "WS_EX_NOACTIVATE is set");
    check((styles & WS_EX_TOOLWINDOW) != 0, "WS_EX_TOOLWINDOW is set");
    check((styles & WS_EX_APPWINDOW) == 0, "WS_EX_APPWINDOW is absent");
    check((styles & WS_EX_TRANSPARENT) == 0, "Window retains mouse interaction");

    const LRESULT activation = SendMessageW(
        window, WM_MOUSEACTIVATE, 0, MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN));
    check(activation == MA_NOACTIVATE, "WM_MOUSEACTIVATE returns MA_NOACTIVATE");
    checkHidden();

    const auto probeEnvironmentMessage = [&](UINT message, WPARAM value,
                                             LPARAM parameter, const char* description) {
        const int changesBefore = environmentChanges;
        SendMessageW(window, message, value, parameter);
        check(environmentChanges > changesBefore, description);
        checkHidden();
    };
    probeEnvironmentMessage(WM_SETTINGCHANGE, 0, 0,
                            "WM_SETTINGCHANGE emits environment notification");

    RECT suggested{};
    if (!GetWindowRect(window, &suggested)) {
        check(false, "Current hidden bounds are available for WM_DPICHANGED");
    } else {
        const UINT dpi = GetDpiForWindow(window);
        check(dpi != 0, "Actual window DPI is available");
        if (dpi != 0) {
            // Keep the real DPI and hidden rectangle: exercise this own-window
            // message without changing desktop settings or showing the panel.
            probeEnvironmentMessage(WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                                    reinterpret_cast<LPARAM>(&suggested),
                                    "WM_DPICHANGED emits environment notification");
        }
    }

    const UINT shellRestartMessage = RegisterWindowMessageW(L"TaskbarCreated");
    check(shellRestartMessage != 0, "TaskbarCreated message can be registered");
    if (shellRestartMessage != 0) {
        const int before = shellRecreations;
        probeEnvironmentMessage(shellRestartMessage, 0, 0,
                                "TaskbarCreated emits environment notification");
        check(shellRecreations > before, "TaskbarCreated emits the shell-recreation signal");
    }

    int powerBefore = powerChanges;
    probeEnvironmentMessage(WM_POWERBROADCAST, PBT_APMSUSPEND, 0,
                            "Suspend notification leaves startup panel hidden");
    check(powerChanges > powerBefore && suspended, "Suspend notification emits suspended state");
    powerBefore = powerChanges;
    probeEnvironmentMessage(WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0,
                            "Resume notification leaves startup panel hidden");
    check(powerChanges > powerBefore && !suspended, "Resume notification emits resumed state");

    // Drain local Qt notifications while the panel is still deliberately hidden.
    QCoreApplication::processEvents();
    checkHidden();
    const HWND foregroundAfter = GetForegroundWindow();
    if (foregroundAfter == foregroundBefore) {
        std::cout << "[OK] Foreground window unchanged during hidden probes\n";
    } else {
        // External applications can change focus at any time. Visibility and
        // the activation message/style assertions above are deterministic.
        std::cout << "[INFO] Foreground window changed externally or during the probe; "
                     "focus observation is inconclusive\n";
    }
    if (application.arguments().contains(QStringLiteral("--visible-regression"))) {
        // Opt-in local regression only; ordinary CTest remains entirely hidden.
        // Temporary windows stay in the work area and close after one second.
        checkVisibleRetention(panel, check);
    }
    std::cout << "RESULT: " << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
