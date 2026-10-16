#include "Configuration.h"
#include "WidgetCatalog.h"
#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <limits>
#include <set>

namespace w11widget::native {
namespace {
bool validId(const std::wstring& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
               (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.';
    });
}
bool validMonitor(const std::wstring& monitor) {
    const std::wstring prefix = L"\\\\.\\DISPLAY";
    if (monitor == L"primary") return true;
    return monitor.size() > prefix.size() && monitor.size() <= 256 && monitor.starts_with(prefix) &&
        monitor[prefix.size()] >= L'1' && monitor[prefix.size()] <= L'9' &&
        std::all_of(monitor.begin() + static_cast<std::ptrdiff_t>(prefix.size()), monitor.end(),
                    [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}
bool nonblank(const std::wstring& value) {
    return std::any_of(value.begin(), value.end(), [](wchar_t c) { return !std::iswspace(c); });
}
bool integer(const JsonValue* value, int low, int high) {
    const auto p = value ? std::get_if<double>(&value->value) : nullptr;
    return p && std::isfinite(*p) && std::trunc(*p) == *p && *p >= low && *p <= high;
}
bool text(const JsonObject& object, const std::wstring& key) {
    const auto p = findValue(object, key);
    return p && std::holds_alternative<std::wstring>(p->value);
}
bool boolean(const JsonObject& object, const std::wstring& key) {
    const auto p = findValue(object, key);
    return p && std::holds_alternative<bool>(p->value);
}
std::size_t stringBytes(const std::wstring& text) {
    std::size_t bytes = 2;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned>(text[i]);
        if (c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') bytes += 2;
        else if (c < 32) bytes += 6;
        else if (c < 128) ++bytes;
        else if (c < 2048) bytes += 2;
        else if (c >= 0xd800 && c <= 0xdbff && i + 1 < text.size() &&
                 text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) { bytes += 4; ++i; }
        else bytes += c <= 0xffff ? 3 : 4;
    }
    return bytes;
}
bool jsonSize(const JsonValue& value, std::size_t& bytes, unsigned depth = 0) {
    if (depth > 64 || bytes > MaximumWidgetSettingsBytes) return false;
    if (const auto numberValue = std::get_if<double>(&value.value)) {
        if (!std::isfinite(*numberValue)) return false;
        char number[64]{}; const auto converted = std::to_chars(number, number + sizeof(number), *numberValue);
        if (converted.ec != std::errc{}) return false;
        bytes += static_cast<std::size_t>(converted.ptr - number);
    }
    else if (const auto stringText = std::get_if<std::wstring>(&value.value)) bytes += stringBytes(*stringText);
    else if (const auto array = std::get_if<JsonArray>(&value.value)) {
        bytes += 2 + (array->empty() ? 0 : array->size() - 1); for (const auto& item : *array) { if (!jsonSize(item, bytes, depth + 1)) return false; }
    } else if (const auto objectValue = std::get_if<JsonObject>(&value.value)) {
        bytes += 2 + (objectValue->empty() ? 0 : objectValue->size() - 1); for (const auto& [key, item] : *objectValue) { bytes += stringBytes(key) + 1; if (!jsonSize(item, bytes, depth + 1)) return false; }
    } else if (const auto booleanValue = std::get_if<bool>(&value.value)) bytes += *booleanValue ? 4 : 5;
    else bytes += 4;
    return bytes <= MaximumWidgetSettingsBytes;
}
const JsonObject& object(const JsonObject& parent, const std::wstring& key) {
    const auto p = findValue(parent, key);
    if (!p || !std::holds_alternative<JsonObject>(p->value)) throw std::wstring(configText::invalidField) + key;
    return std::get<JsonObject>(p->value);
}
std::wstring requiredText(const JsonObject& obj, const std::wstring& key) {
    if (!text(obj, key)) throw std::wstring(configText::invalidField) + key;
    return stringValue(obj, key);
}
int requiredInt(const JsonObject& obj, const std::wstring& key, int low, int high) {
    if (!integer(findValue(obj, key), low, high)) throw std::wstring(configText::invalidField) + key;
    return intValue(obj, key);
}
bool requiredBool(const JsonObject& obj, const std::wstring& key) {
    if (!boolean(obj, key)) throw std::wstring(configText::invalidField) + key;
    return boolValue(obj, key);
}
void removeKeys(JsonObject& object, std::initializer_list<const wchar_t*> keys) {
    for (const auto key : keys) object.erase(key);
}
bool migrateGroups(std::vector<WidgetConfiguration>& widgets, bool& migrated,
                   std::wstring& error, const TimeZoneValidator& zoneValidator) {
    std::set<std::wstring> reserved;
    for (const auto& widget : widgets) reserved.insert(widget.instanceId);
    std::vector<WidgetConfiguration> result;
    for (const auto& widget : widgets) {
        const bool singular = widget.settings.contains(L"time_zone");
        const auto legacy = findValue(widget.settings, L"time_zones");
        if (widget.typeId != L"world-clock" || (singular && !legacy)) { result.push_back(widget); continue; }
        migrated = true;
        if (singular && legacy) { error = configText::groupConflict; return false; }
        JsonArray zones{L"UTC", L"America/New_York", L"Europe/Istanbul"};
        if (legacy) {
            const auto p = std::get_if<JsonArray>(&legacy->value);
            if (!p) { error = std::wstring(configText::invalidField) + L"time_zones"; return false; }
            zones = *p;
        }
        if (result.size() + zones.size() > MaximumWidgetCount) { error = configText::widgetLimit; return false; }
        for (std::size_t index = 0; index < zones.size(); ++index) {
            const auto zone = std::get_if<std::wstring>(&zones[index].value);
            if (!zone || zone->empty() || (zoneValidator && !zoneValidator(*zone))) { error = configText::invalidZone; return false; }
            auto clock = widget;
            clock.widthDip = std::clamp(widget.widthDip / static_cast<int>(zones.size()), 48, 320);
            clock.settings.erase(L"time_zones"); clock.settings[L"time_zone"] = *zone;
            if (index != 0) {
                for (std::size_t suffix = index + 1;; ++suffix) {
                    const auto end = L"-" + std::to_wstring(suffix);
                    const auto id = widget.instanceId.substr(0, 64 - end.size()) + end;
                    if (reserved.insert(id).second) { clock.instanceId = id; break; }
                }
            }
            result.push_back(std::move(clock));
        }
    }
    if (result.size() > MaximumWidgetCount) { error = configText::widgetLimit; return false; }
    if (migrated) std::stable_partition(result.begin(), result.end(), [](const auto& w) { return w.typeId == L"countdown"; });
    widgets = std::move(result);
    return true;
}
}

const JsonValue* findValue(const JsonObject& object, const std::wstring& key) {
    const auto item = object.find(key); return item == object.end() ? nullptr : &item->second;
}
std::wstring stringValue(const JsonObject& object, const std::wstring& key, std::wstring fallback) {
    const auto value = findValue(object, key); const auto p = value ? std::get_if<std::wstring>(&value->value) : nullptr;
    return p ? *p : std::move(fallback);
}
bool boolValue(const JsonObject& object, const std::wstring& key, bool fallback) {
    const auto value = findValue(object, key); const auto p = value ? std::get_if<bool>(&value->value) : nullptr;
    return p ? *p : fallback;
}
int intValue(const JsonObject& object, const std::wstring& key, int fallback) {
    return integer(findValue(object, key), std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) ?
        static_cast<int>(std::get<double>(findValue(object, key)->value)) : fallback;
}
WorldClockSettings worldClockSettings(const WidgetConfiguration& w) {
    return {stringValue(w.settings, L"label"), stringValue(w.settings, L"time_zone", L"UTC"), boolValue(w.settings, L"show_date")};
}
CountdownSettings countdownSettings(const WidgetConfiguration& w) {
    return {stringValue(w.settings, L"label", configText::countdown), stringValue(w.settings, L"target_time", L"18:00"),
        stringValue(w.settings, L"time_zone", L"Europe/Istanbul"), boolValue(w.settings, L"daily_repeat", true), stringValue(w.settings, L"target_date")};
}
FocusSettings focusSettings(const WidgetConfiguration& w) { return {stringValue(w.settings, L"label", configText::focus), intValue(w.settings, L"duration_minutes", 25)}; }
WidgetConfiguration defaultWidget(const std::wstring& type, const std::wstring& id) {
    WidgetConfiguration w; w.instanceId = id; w.typeId = type;
    if (const auto descriptor = findWidgetDescriptor(type); descriptor && descriptor->availability == WidgetAvailability::Implemented)
        w.widthDip = descriptor->defaultWidthDip;
    if (type == L"world-clock") w.settings = {{L"time_zone", L"UTC"}, {L"show_date", false}};
    else if (type == L"countdown") w.settings = {{L"label", configText::countdown}, {L"target_time", L"18:00"}, {L"daily_repeat", true}, {L"time_zone", L"Europe/Istanbul"}, {L"target_date", L""}};
    else if (type == L"focus-timer") w.settings = {{L"label", configText::focus}, {L"duration_minutes", 25}};
    return w;
}
PanelConfiguration defaultConfiguration() {
    PanelConfiguration c; c.widgets.push_back(defaultWidget(L"countdown", L"countdown-1"));
    const wchar_t* zones[]{L"UTC", L"America/New_York", L"Europe/Istanbul"};
    for (int i = 0; i < 3; ++i) { auto w = defaultWidget(L"world-clock", L"world-clock-" + std::to_wstring(i + 1)); w.settings[L"time_zone"] = zones[i]; c.widgets.push_back(std::move(w)); }
    return c;
}
bool validTime(const std::wstring& v) {
    return v.size() == 5 && v[2] == L':' && v[0] >= L'0' && v[0] <= L'2' && v[1] >= L'0' && v[1] <= L'9' &&
        (v[0] != L'2' || v[1] <= L'3') && v[3] >= L'0' && v[3] <= L'5' && v[4] >= L'0' && v[4] <= L'9';
}
bool validDate(const std::wstring& v) {
    if (v.size() != 10 || v[4] != L'-' || v[7] != L'-') return false;
    for (std::size_t i = 0; i < v.size(); ++i) if (i != 4 && i != 7 && (v[i] < L'0' || v[i] > L'9')) return false;
    const int y = std::stoi(v.substr(0, 4)), m = std::stoi(v.substr(5, 2)), d = std::stoi(v.substr(8, 2));
    return y >= 1 && std::chrono::year_month_day{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{static_cast<unsigned>(d)}}.ok();
}
bool validateConfiguration(const PanelConfiguration& c, std::wstring& error, const TimeZoneValidator& zoneValidator) {
    error.clear();
    const auto fail = [&](const wchar_t* field) { error = std::wstring(configText::invalidField) + field; return false; };
    if (!validMonitor(c.monitor)) return fail(L"monitor");
    if (c.side != PanelSide::Left && c.side != PanelSide::Right) return fail(L"side");
    if (c.widthDip < 80 || c.widthDip > 640) return fail(L"width_dip");
    if (c.offsetDip < 0 || c.offsetDip > 20000) return fail(L"offset_dip");
    if (!std::isfinite(c.opacity) || c.opacity < .25 || c.opacity > 1) return fail(L"opacity");
    if (c.theme != L"system" && c.theme != L"light" && c.theme != L"dark") return fail(L"theme");
    if (c.regionConfirmed && (!validMonitor(c.regionContext.monitor) || c.regionContext.monitor == L"primary" ||
        c.regionContext.dpi < 48 || c.regionContext.dpi > 768 || c.regionContext.monitorRight <= c.regionContext.monitorLeft ||
        c.regionContext.monitorBottom <= c.regionContext.monitorTop || c.regionContext.taskbarRight <= c.regionContext.taskbarLeft ||
        c.regionContext.taskbarBottom <= c.regionContext.taskbarTop)) return fail(L"region_context");
    if (c.widgets.size() > MaximumWidgetCount) { error = configText::widgetLimit; return false; }
    std::set<std::wstring> ids; std::size_t bytes = 0;
    for (const auto& w : c.widgets) {
        if (!validId(w.instanceId) || !validId(w.typeId) || !ids.insert(w.instanceId).second || w.widthDip < 48 || w.widthDip > 320) { error = configText::invalidWidget; return false; }
        if (!jsonSize(w.settings, bytes)) { error = configText::invalidSettings; return false; }
        if (w.typeId == L"world-clock" || w.typeId == L"countdown") {
            if (!text(w.settings, L"time_zone")) return fail(L"time_zone");
            const auto zone = stringValue(w.settings, L"time_zone");
            if (zone.empty() || zone.size() > 256 || (zoneValidator && !zoneValidator(zone))) { error = std::wstring(configText::invalidZone) + zone; return false; }
        }
        if (w.typeId == L"world-clock") {
            if (!boolean(w.settings, L"show_date")) return fail(L"show_date");
            if (w.settings.contains(L"label") && (!text(w.settings, L"label") || stringValue(w.settings, L"label").size() > 40)) return fail(L"label");
        } else if (w.typeId == L"countdown" || w.typeId == L"focus-timer") {
            const auto label = stringValue(w.settings, L"label");
            if (!text(w.settings, L"label") || label.size() > 40 || !nonblank(label)) return fail(L"label");
            if (w.typeId == L"countdown") {
                if (!text(w.settings, L"target_time") || !validTime(stringValue(w.settings, L"target_time"))) return fail(L"target_time");
                if (!boolean(w.settings, L"daily_repeat")) return fail(L"daily_repeat");
                if (!text(w.settings, L"target_date") || (!stringValue(w.settings, L"target_date").empty() && !validDate(stringValue(w.settings, L"target_date")))) return fail(L"target_date");
            } else if (!integer(findValue(w.settings, L"duration_minutes"), 1, 180)) return fail(L"duration_minutes");
        }
    }
    return true;
}

bool decodeConfiguration(const JsonObject& root, PanelConfiguration& output, bool& migrated, std::wstring& error, const TimeZoneValidator& zones) {
    migrated = false; error.clear();
    try {
        const int version = requiredInt(root, L"schema_version", 1, 2);
        auto c = defaultConfiguration(); c.extra = root;
        removeKeys(c.extra, {L"schema_version", L"panel", L"widgets"});
        const auto& p = object(root, L"panel"); c.panelExtra = p;
        c.monitor = requiredText(p, L"monitor");
        const auto side = requiredText(p, L"side");
        if (side != L"left" && side != L"right") throw std::wstring(configText::invalidField) + L"side";
        c.side = side == L"left" ? PanelSide::Left : PanelSide::Right;
        c.widthDip = requiredInt(p, L"width_dip", 80, 640); c.offsetDip = requiredInt(p, L"offset_dip", 0, 20000);
        const auto opacity = findValue(p, L"opacity");
        if (!opacity || !std::holds_alternative<double>(opacity->value)) throw std::wstring(configText::invalidField) + L"opacity";
        c.opacity = std::get<double>(opacity->value); requiredBool(p, L"hide_for_fullscreen");
        if (p.contains(L"theme")) c.theme = requiredText(p, L"theme");
        if (p.contains(L"region_confirmed")) c.regionConfirmed = requiredBool(p, L"region_confirmed");
        if (p.contains(L"region_context")) {
            const auto& r = object(p, L"region_context"); auto& rc = c.regionContext;
            c.regionContextExtra = r;
            rc.monitor = requiredText(r, L"monitor");
            const auto coordinate = [&](const wchar_t* key) { return requiredInt(r, key, -1000000, 1000000); };
            rc.monitorLeft = coordinate(L"monitor_left"); rc.monitorTop = coordinate(L"monitor_top");
            rc.monitorRight = coordinate(L"monitor_right"); rc.monitorBottom = coordinate(L"monitor_bottom");
            rc.taskbarLeft = coordinate(L"taskbar_left"); rc.taskbarTop = coordinate(L"taskbar_top");
            rc.taskbarRight = coordinate(L"taskbar_right"); rc.taskbarBottom = coordinate(L"taskbar_bottom");
            rc.dpi = requiredInt(r, L"dpi", 48, 768);
            removeKeys(c.regionContextExtra, {L"monitor", L"monitor_left", L"monitor_top", L"monitor_right", L"monitor_bottom",
                L"taskbar_left", L"taskbar_top", L"taskbar_right", L"taskbar_bottom", L"dpi"});
        }
        removeKeys(c.panelExtra, {L"monitor", L"side", L"width_dip", L"offset_dip", L"opacity", L"hide_for_fullscreen", L"theme", L"region_confirmed", L"region_context"});
        if (version == 2) {
            const auto entries = findValue(root, L"widgets");
            if (!entries || !std::holds_alternative<JsonArray>(entries->value)) throw std::wstring(configText::invalidField) + L"widgets";
            const auto& list = std::get<JsonArray>(entries->value);
            if (list.size() > MaximumWidgetCount) throw std::wstring(configText::widgetLimit);
            c.widgets.clear(); std::set<std::wstring> ids;
            for (const auto& entry : list) {
                const auto obj = std::get_if<JsonObject>(&entry.value);
                if (!obj) throw std::wstring(configText::invalidField) + L"widgets";
                WidgetConfiguration w; w.extra = *obj;
                w.instanceId = requiredText(*obj, L"instance_id"); w.typeId = requiredText(*obj, L"type_id");
                if (!validId(w.instanceId) || !validId(w.typeId) || !ids.insert(w.instanceId).second) throw std::wstring(configText::invalidWidget);
                w.enabled = requiredBool(*obj, L"enabled"); w.visible = requiredBool(*obj, L"visible");
                w.widthDip = requiredInt(*obj, L"width_dip", 48, 320); w.settings = object(*obj, L"settings");
                removeKeys(w.extra, {L"instance_id", L"type_id", L"enabled", L"visible", L"width_dip", L"settings"});
                c.widgets.push_back(std::move(w));
            }
            if (!migrateGroups(c.widgets, migrated, error, zones)) return false;
        } else migrated = true;
        if (!validateConfiguration(c, error, zones)) return false;
        output = std::move(c); return true;
    } catch (const std::wstring& message) { error = message; return false; }
}
JsonObject encodeConfiguration(const PanelConfiguration& c) {
    auto root = c.extra; auto panel = c.panelExtra;
    panel[L"monitor"] = c.monitor; panel[L"side"] = c.side == PanelSide::Left ? L"left" : L"right";
    panel[L"width_dip"] = c.widthDip; panel[L"offset_dip"] = c.offsetDip; panel[L"opacity"] = c.opacity;
    panel[L"hide_for_fullscreen"] = false; panel[L"theme"] = c.theme; panel[L"region_confirmed"] = c.regionConfirmed;
    if (!c.regionContext.monitor.empty() || !c.regionContextExtra.empty()) {
        const auto& r = c.regionContext;
        JsonObject region{{L"monitor", r.monitor}, {L"monitor_left", r.monitorLeft}, {L"monitor_top", r.monitorTop},
            {L"monitor_right", r.monitorRight}, {L"monitor_bottom", r.monitorBottom}, {L"taskbar_left", r.taskbarLeft},
            {L"taskbar_top", r.taskbarTop}, {L"taskbar_right", r.taskbarRight}, {L"taskbar_bottom", r.taskbarBottom}, {L"dpi", r.dpi}};
        for (const auto& [key, value] : c.regionContextExtra) region.try_emplace(key, value);
        panel[L"region_context"] = std::move(region);
    }
    JsonArray widgets;
    for (const auto& w : c.widgets) {
        auto item = w.extra; item[L"instance_id"] = w.instanceId; item[L"type_id"] = w.typeId;
        item[L"enabled"] = w.enabled; item[L"visible"] = w.visible; item[L"width_dip"] = w.widthDip; item[L"settings"] = w.settings;
        widgets.emplace_back(std::move(item));
    }
    root[L"schema_version"] = 2; root[L"panel"] = std::move(panel); root[L"widgets"] = std::move(widgets); return root;
}
}
