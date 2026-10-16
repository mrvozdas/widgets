#pragma once

#include <span>
#include <string_view>

namespace w11widget::native {

enum class WidgetAvailability { Implemented, Planned };
enum class WidgetRefreshPolicy {
    MinuteBoundary, SecondWhileRunning, DayBoundary, SharedVisibleSampling,
    PowerEvents, NetworkEvents, ProviderDecisionRequired
};
enum class WidgetPopupKind {
    ClockDetails, CountdownDetails, FocusControls, CalendarView, SystemDetails,
    PowerDetails, NetworkDetails, Forecast, AccountEvents
};
struct WidgetDescriptor {
    std::wstring_view typeId;
    std::wstring_view name;
    WidgetAvailability availability;
    int defaultWidthDip;
    WidgetRefreshPolicy refreshPolicy;
    // Known keys describe implemented editors. Planned entries have no settings
    // schema yet; they must not become addable merely because metadata exists.
    std::span<const std::wstring_view> settingKeys;
    WidgetPopupKind popupKind;
};

// Planned metadata expresses integration requirements, never a provider/runtime.
std::span<const WidgetDescriptor> widgetCatalog() noexcept;
const WidgetDescriptor* findWidgetDescriptor(std::wstring_view typeId) noexcept;
bool isImplementedWidgetType(std::wstring_view typeId) noexcept;

} // namespace w11widget::native
