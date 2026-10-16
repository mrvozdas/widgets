#pragma once

#include <QJsonObject>
#include <QString>
#include <vector>

namespace w11widget {

struct WidgetConfiguration {
    QString instanceId;
    QString typeId;
    bool enabled = true;
    bool visible = true;
    int widthDip = 120;
    QJsonObject settings;

    bool operator==(const WidgetConfiguration&) const = default;
};

using WidgetConfigurations = std::vector<WidgetConfiguration>;

inline WidgetConfigurations defaultWidgetConfigurations()
{
    return {
        {QStringLiteral("countdown-1"), QStringLiteral("countdown"), true, true, 72,
         {{QStringLiteral("label"), QStringLiteral("Geri sayım")},
          {QStringLiteral("target_time"), QStringLiteral("18:00")},
          {QStringLiteral("daily_repeat"), true},
          {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")},
          {QStringLiteral("target_date"), QString{}}}},
        {QStringLiteral("world-clock-1"), QStringLiteral("world-clock"), true, true, 72,
         {{QStringLiteral("show_date"), false}, {QStringLiteral("time_zone"), QStringLiteral("UTC")}}},
        {QStringLiteral("world-clock-2"), QStringLiteral("world-clock"), true, true, 72,
         {{QStringLiteral("show_date"), false}, {QStringLiteral("time_zone"), QStringLiteral("America/New_York")}}},
        {QStringLiteral("world-clock-3"), QStringLiteral("world-clock"), true, true, 72,
         {{QStringLiteral("show_date"), false}, {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")}}}
    };
}

// Shared structural limits for persistence and runtime configuration.
inline constexpr int MaximumWidgetCount = 16;
inline constexpr int MinimumWidgetWidthDip = 48;
inline constexpr int MaximumWidgetWidthDip = 320;

} // namespace w11widget
