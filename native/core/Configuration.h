#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <utility>
#include "strings/tr.h"

namespace w11widget::native {

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::wstring, JsonValue>;
    using Value = std::variant<std::nullptr_t, bool, double, std::wstring, Array, Object>;
    Value value = nullptr;
    JsonValue() = default;
    JsonValue(std::nullptr_t) : value(nullptr) {}
    JsonValue(bool input) : value(input) {}
    JsonValue(int input) : value(static_cast<double>(input)) {}
    JsonValue(double input) : value(input) {}
    JsonValue(const wchar_t* input) : value(std::wstring(input)) {}
    JsonValue(std::wstring input) : value(std::move(input)) {}
    JsonValue(Array input) : value(std::move(input)) {}
    JsonValue(Object input) : value(std::move(input)) {}
    bool operator==(const JsonValue&) const = default;
};
using JsonObject = JsonValue::Object;
using JsonArray = JsonValue::Array;

enum class PanelSide { Left, Right };
struct RegionContext {
    std::wstring monitor;
    int monitorLeft = 0, monitorTop = 0, monitorRight = 0, monitorBottom = 0;
    int taskbarLeft = 0, taskbarTop = 0, taskbarRight = 0, taskbarBottom = 0;
    int dpi = 96;
    bool operator==(const RegionContext&) const = default;
};
struct WidgetConfiguration {
    std::wstring instanceId;
    std::wstring typeId;
    bool enabled = true;
    bool visible = true;
    int widthDip = 72;
    JsonObject settings;
    JsonObject extra;
    bool operator==(const WidgetConfiguration&) const = default;
};
struct PanelConfiguration {
    std::wstring monitor = L"primary";
    PanelSide side = PanelSide::Left;
    int widthDip = 330;
    int offsetDip = 150;
    double opacity = 0.95;
    std::wstring theme = L"system";
    bool regionConfirmed = false;
    RegionContext regionContext;
    std::vector<WidgetConfiguration> widgets;
    JsonObject extra;
    JsonObject panelExtra;
    // Future context metadata roundtrips without altering physical placement equality.
    JsonObject regionContextExtra;
    bool operator==(const PanelConfiguration&) const = default;
};
struct WorldClockSettings { std::wstring label; std::wstring timeZone = L"UTC"; bool showDate = false; };
struct CountdownSettings {
    std::wstring label = configText::countdown;
    std::wstring targetTime = L"18:00";
    std::wstring timeZone = L"Europe/Istanbul";
    bool dailyRepeat = true;
    std::wstring targetDate;
};
struct FocusSettings { std::wstring label = configText::focus; int durationMinutes = 25; };

inline constexpr std::size_t MaximumWidgetCount = 16;
inline constexpr std::size_t MaximumSettingsBytes = 256 * 1024;
inline constexpr std::size_t MaximumWidgetSettingsBytes = 64 * 1024;

const JsonValue* findValue(const JsonObject& object, const std::wstring& key);
std::wstring stringValue(const JsonObject& object, const std::wstring& key, std::wstring fallback = {});
bool boolValue(const JsonObject& object, const std::wstring& key, bool fallback = false);
int intValue(const JsonObject& object, const std::wstring& key, int fallback = 0);
WorldClockSettings worldClockSettings(const WidgetConfiguration& widget);
CountdownSettings countdownSettings(const WidgetConfiguration& widget);
FocusSettings focusSettings(const WidgetConfiguration& widget);
WidgetConfiguration defaultWidget(const std::wstring& typeId, const std::wstring& instanceId);
PanelConfiguration defaultConfiguration();
bool validDate(const std::wstring& value);
bool validTime(const std::wstring& value);
using TimeZoneValidator = std::function<bool(const std::wstring&)>;
bool validateConfiguration(const PanelConfiguration& configuration, std::wstring& error,
                           const TimeZoneValidator& validateZone = {});
// Structural parsing preserves unfamiliar types/settings/fields. Invalid known settings fail validation.
bool decodeConfiguration(const JsonObject& root, PanelConfiguration& configuration,
                         bool& migrated, std::wstring& error, const TimeZoneValidator& validateZone = {});
JsonObject encodeConfiguration(const PanelConfiguration& configuration);

} // namespace w11widget::native
