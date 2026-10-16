#include "WidgetCatalog.h"
#include "strings/tr.h"
#include <algorithm>
#include <array>

namespace w11widget::native {
namespace {
constexpr std::wstring_view clockKeys[]{L"label", L"time_zone", L"show_date"};
constexpr std::wstring_view countdownKeys[]{L"label", L"target_time", L"daily_repeat", L"time_zone", L"target_date"};
constexpr std::wstring_view focusKeys[]{L"label", L"duration_minutes"};
constexpr WidgetDescriptor descriptors[]{
    {L"world-clock", configText::worldClock, WidgetAvailability::Implemented, 72,
     WidgetRefreshPolicy::MinuteBoundary, clockKeys, WidgetPopupKind::ClockDetails},
    {L"countdown", configText::countdown, WidgetAvailability::Implemented, 72,
     WidgetRefreshPolicy::MinuteBoundary, countdownKeys, WidgetPopupKind::CountdownDetails},
    {L"focus-timer", configText::focusTimer, WidgetAvailability::Implemented, 72,
     WidgetRefreshPolicy::SecondWhileRunning, focusKeys, WidgetPopupKind::FocusControls},
    {L"calendar", configText::calendar, WidgetAvailability::Planned, 144,
     WidgetRefreshPolicy::DayBoundary, {}, WidgetPopupKind::CalendarView},
    {L"system-status", configText::systemStatus, WidgetAvailability::Planned, 120,
     WidgetRefreshPolicy::SharedVisibleSampling, {}, WidgetPopupKind::SystemDetails},
    {L"power", configText::power, WidgetAvailability::Planned, 72,
     WidgetRefreshPolicy::PowerEvents, {}, WidgetPopupKind::PowerDetails},
    {L"network", configText::network, WidgetAvailability::Planned, 96,
     WidgetRefreshPolicy::NetworkEvents, {}, WidgetPopupKind::NetworkDetails},
    {L"weather", configText::weather, WidgetAvailability::Planned, 120,
     WidgetRefreshPolicy::ProviderDecisionRequired, {}, WidgetPopupKind::Forecast},
    {L"account-calendar", configText::accountCalendar, WidgetAvailability::Planned, 160,
     WidgetRefreshPolicy::ProviderDecisionRequired, {}, WidgetPopupKind::AccountEvents}
};
}
std::span<const WidgetDescriptor> widgetCatalog() noexcept { return descriptors; }
const WidgetDescriptor* findWidgetDescriptor(std::wstring_view typeId) noexcept {
    const auto result = std::find_if(std::begin(descriptors), std::end(descriptors),
        [typeId](const auto& entry) { return entry.typeId == typeId; });
    return result == std::end(descriptors) ? nullptr : result;
}
bool isImplementedWidgetType(std::wstring_view typeId) noexcept {
    const auto descriptor = findWidgetDescriptor(typeId);
    return descriptor && descriptor->availability == WidgetAvailability::Implemented;
}
}
