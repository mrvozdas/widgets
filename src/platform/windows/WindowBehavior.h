#pragma once

#include "core/PanelPositioner.h"

#include <QString>
#include <QtGui/qwindowdefs.h>
#include <functional>
#include <memory>

namespace w11widget::windows {

struct PanelNativeMessage {
    bool handled = false;
    // Notify the controller to refresh geometry; never hide the panel here.
    bool placementInvalidated = false;
    qintptr result = 0;
    // 0: none, 1: suspend, 2: resume. Notification handling remains with Qt.
    int powerTransition = 0;
    bool shellRecreated = false;
    bool stackingChanged = false;
    bool compositionChanged = false;
    bool clockChanged = false;
};

// Out-of-context notifications scoped to the observed taskbar's process.
// No code is installed in Explorer and no other HWND is modified.
class TaskbarZOrderObserver final {
public:
    explicit TaskbarZOrderObserver(std::function<void()> changed);
    ~TaskbarZOrderObserver();
    TaskbarZOrderObserver(const TaskbarZOrderObserver&) = delete;
    TaskbarZOrderObserver& operator=(const TaskbarZOrderObserver&) = delete;
    bool observe(WId taskbarWindow, QString& error);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// These operations only modify the window owned by PanelWindow.
bool preparePanelWindow(WId nativeWindow, QString& error);
// Prevent DWM Peek fading and transitions on our own window, before display.
bool protectPanelComposition(WId nativeWindow, QString& error);
bool placePanelWindow(WId nativeWindow, const Rect& requestedBounds,
                      bool showWindow, Rect& actualBounds, QString& error);
PanelNativeMessage inspectPanelNativeMessage(void* nativeMessage, bool keepVisible = false);

} // namespace w11widget::windows
