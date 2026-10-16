#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/PanelPositioner.h"

#include <optional>
#include <string>
#include <vector>

namespace w11widget::windows {

struct MonitorDescription {
    std::wstring device;
    Rect bounds{};
    bool primary = false;
};

struct TaskbarSnapshot {
    HWND hwnd = nullptr;
    Rect bounds{};
    Rect monitorBounds{};
    Rect workArea{};
    unsigned edge = 0;
    unsigned dpi = 0;
    bool autoHide = false;
    bool primary = true;
    std::wstring monitorDevice;
};

// Read-only discovery. A missing or inconsistent required probe fails closed.
[[nodiscard]] std::vector<MonitorDescription> enumerateMonitors(std::string& error);
[[nodiscard]] std::optional<TaskbarSnapshot> readTaskbar(std::string& error);
[[nodiscard]] std::optional<TaskbarSnapshot> readTaskbarForMonitor(
    const std::wstring& device, std::string& error);
// Pure derivation from a full-width, horizontal window on an observed edge.
[[nodiscard]] std::optional<unsigned> observedHorizontalEdge(
    const Rect& taskbar, const Rect& monitorBounds);
[[nodiscard]] std::string describe(const TaskbarSnapshot& snapshot);
[[nodiscard]] bool hasFullscreenConflict(const TaskbarSnapshot& snapshot,
                                         HWND ownWindow = nullptr);

} // namespace w11widget::windows
