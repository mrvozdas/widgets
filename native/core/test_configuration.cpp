#include "Configuration.h"
#include "WidgetCatalog.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <cstdlib>
#include <set>

using namespace w11widget::native;
namespace {
int checks = 0;
void require(bool condition, const char* description) {
    ++checks; if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
}
int main() {
    std::wstring error; bool migrated = false;
    auto defaults = defaultConfiguration(); PanelConfiguration decoded;
    require(validateConfiguration(defaults, error), "default structure");
    require(decodeConfiguration(encodeConfiguration(defaults), decoded, migrated, error) && decoded == defaults && !migrated, "schema2 roundtrip");
    auto c = defaults;
    c.extra[L"future_root"] = JsonArray{true, nullptr, L"Türkçe"}; c.panelExtra[L"future_panel"] = 42;
    auto unknown = defaultWidget(L"future-widget", L"future-1"); unknown.settings[L"nested"] = JsonObject{{L"array", JsonArray{1, false, L"ö"}}}; unknown.extra[L"future_widget"] = L"kept";
    c.widgets.push_back(unknown); c.widgets[0].settings[L"future_countdown"] = L"retained";
    require(decodeConfiguration(encodeConfiguration(c), decoded, migrated, error) && decoded == c, "unknown root panel widget and settings preservation");
    c = defaults; c.widgets[1].instanceId = c.widgets[0].instanceId;
    require(!validateConfiguration(c, error), "duplicate ids rejected");
    c = defaults; c.widgets[0].instanceId = L"bad/id"; require(!validateConfiguration(c, error), "unsafe id rejected");
    c = defaults; c.widgets[0].widthDip = 47; require(!validateConfiguration(c, error), "widget width lower bound");
    c = defaults; c.opacity = std::numeric_limits<double>::quiet_NaN(); require(!validateConfiguration(c, error), "nonfinite opacity");
    c = defaults; c.monitor = L"\\\\.\\DISPLAY0"; require(!validateConfiguration(c, error), "monitor zero rejected");
    c.monitor = L"\\\\.\\DISPLAY12"; require(validateConfiguration(c, error), "monitor numeric accepted");
    c = defaults; c.widgets[0].settings[L"target_time"] = L"24:00"; require(!validateConfiguration(c, error), "invalid time");
    c = defaults; c.widgets[0].settings[L"target_date"] = L"2026-02-29"; require(!validateConfiguration(c, error), "invalid date");
    require(validDate(L"2028-02-29") && !validDate(L"2026-2-01") && !validDate(L"0000-01-01"), "strict ISO leap date");
    c = defaults; c.widgets.push_back(defaultWidget(L"focus-timer", L"focus-1"));
    c.widgets.back().settings[L"duration_minutes"] = 1.5; require(!validateConfiguration(c, error), "fractional focus duration");
    c.widgets.back().settings[L"duration_minutes"] = 180; require(validateConfiguration(c, error), "focus upper bound");
    c.widgets.back().settings[L"duration_minutes"] = 181; require(!validateConfiguration(c, error), "focus above bound");
    c = defaults; c.widgets[0].settings[L"label"] = L" \t"; require(!validateConfiguration(c, error), "blank label");
    c = defaults; c.widgets[0].settings[L"extra"] = std::wstring(MaximumWidgetSettingsBytes, L'a'); require(!validateConfiguration(c, error), "settings allocation bound");
    c = defaults; c.widgets.resize(17, c.widgets[0]); require(!validateConfiguration(c, error), "widget count bound");
    c = defaults; c.regionConfirmed = true; require(!validateConfiguration(c, error), "confirmed region requires provenance");
    c.regionContext = {L"\\\\.\\DISPLAY1", 0, 0, 1920, 1080, 0, 1040, 1920, 1080, 96};
    require(decodeConfiguration(encodeConfiguration(c), decoded, migrated, error) && decoded == c, "region context roundtrip");
    const auto geometry = c.regionContext;
    c.regionContextExtra[L"future_context"] = JsonObject{{L"version", 7}, {L"metadata", JsonArray{nullptr, L"retained"}}};
    require(decodeConfiguration(encodeConfiguration(c), decoded, migrated, error) && decoded == c && decoded.regionContext == geometry,
            "unknown region metadata roundtrips without changing geometry equality");
    auto root = encodeConfiguration(defaults); auto& panel = std::get<JsonObject>(root[L"panel"].value);
    panel[L"offset_dip"] = 1.5; require(!decodeConfiguration(root, decoded, migrated, error), "fractional layout rejected on read");
    root = encodeConfiguration(defaults); root[L"schema_version"] = 3; require(!decodeConfiguration(root, decoded, migrated, error), "future schema rejected");
    root[L"schema_version"] = 1; root[L"widgets"] = L"ignored legacy extension";
    require(decodeConfiguration(root, decoded, migrated, error) && migrated && decoded.widgets == defaults.widgets, "schema1 defaults ignoring old widget extension");
    c = defaults; c.widgets.resize(2); auto& group = c.widgets[1]; group.widthDip = 240; group.settings.erase(L"time_zone");
    group.settings[L"time_zones"] = JsonArray{L"UTC", L"Asia/Tokyo", L"Europe/Istanbul"};
    c.widgets.push_back(defaultWidget(L"future", L"world-clock-1-2"));
    require(decodeConfiguration(encodeConfiguration(c), decoded, migrated, error) && migrated && decoded.widgets.size() == 5, "group expansion");
    require(decoded.widgets[2].instanceId == L"world-clock-1-3" && decoded.widgets[2].widthDip == 80 && stringValue(decoded.widgets[2].settings, L"time_zone") == L"Asia/Tokyo", "split id reserves later ids and divides width");
    c.widgets[1].settings[L"time_zone"] = L"UTC"; require(!decodeConfiguration(encodeConfiguration(c), decoded, migrated, error), "conflicting singular and grouped zones");
    c = defaults; c.widgets.resize(2); c.widgets[1].settings.erase(L"time_zone"); c.widgets[1].settings[L"time_zones"] = JsonArray{};
    require(decodeConfiguration(encodeConfiguration(c), decoded, migrated, error) && decoded.widgets.size() == 1, "empty legacy clock group preserved");
    c.widgets[1].settings[L"time_zones"] = JsonArray(17, JsonValue(L"UTC")); require(!decodeConfiguration(encodeConfiguration(c), decoded, migrated, error), "expansion bounded before copying");
    c = defaults; require(!validateConfiguration(c, error, [](const std::wstring&) { return false; }), "platform zone validator honored");
    std::set<std::wstring_view> catalogIds;
    std::size_t implemented = 0, planned = 0;
    for (const auto& descriptor : widgetCatalog()) {
        require(catalogIds.insert(descriptor.typeId).second && !descriptor.name.empty(), "catalog id unique and localized name nonempty");
        require(descriptor.defaultWidthDip >= 48 && descriptor.defaultWidthDip <= 320, "catalog widths obey persistence bounds");
        if (descriptor.availability == WidgetAvailability::Implemented) {
            ++implemented;
            auto single = defaultConfiguration(); single.widgets = {defaultWidget(std::wstring(descriptor.typeId), L"catalog-test")};
            require(validateConfiguration(single, error) && single.widgets.front().widthDip == descriptor.defaultWidthDip, "implemented catalog has valid matching defaults");
            for (const auto& [key, value] : single.widgets.front().settings) {
                (void)value;
                require(std::find(descriptor.settingKeys.begin(), descriptor.settingKeys.end(), key) != descriptor.settingKeys.end(), "implemented default setting covered by catalog");
            }
        } else {
            ++planned;
            require(!isImplementedWidgetType(descriptor.typeId) && descriptor.settingKeys.empty(), "planned metadata cannot advertise an implemented settings schema");
        }
    }
    require(implemented == 3 && planned == 6, "implemented and explicitly planned catalog boundaries");
    for (const auto& widget : defaults.widgets) require(isImplementedWidgetType(widget.typeId), "every startup default has an implemented descriptor");
    require(!findWidgetDescriptor(L"future-unregistered") && !isImplementedWidgetType(L"future-unregistered"), "unregistered types are not advertised");
    c = defaults; c.widgets.push_back(defaultWidget(L"weather", L"planned-legacy"));
    c.widgets.back().settings[L"provider_unknown"] = L"retained";
    require(decodeConfiguration(encodeConfiguration(c), decoded, migrated, error) && decoded == c, "planned and unknown saved types still roundtrip without provider activation");
    std::cout << "Native configuration: " << checks << " checks PASS\n";
}
