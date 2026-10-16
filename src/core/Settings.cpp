#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <utility>

namespace w11widget {
namespace {

constexpr qsizetype MaximumWidgetSettingsBytes = 64 * 1024;

bool validWidgetId(const QString& id) {
    if (id.isEmpty() || id.size() > 64) return false;
    for (const QChar character : id) {
        if ((character >= QLatin1Char('a') && character <= QLatin1Char('z')) ||
            (character >= QLatin1Char('A') && character <= QLatin1Char('Z')) ||
            (character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
            character == QLatin1Char('-') || character == QLatin1Char('_') ||
            character == QLatin1Char('.')) continue;
        return false;
    }
    return true;
}

bool validateWidgets(const WidgetConfigurations& widgets, QString& error) {
    if (widgets.size() > MaximumWidgetCount) {
        error = QStringLiteral("widgets: en fazla %1 widget kullanılabilir.").arg(MaximumWidgetCount);
        return false;
    }
    QSet<QString> instanceIds;
    qsizetype settingsBytes = 0;
    for (const auto& widget : widgets) {
        if (!validWidgetId(widget.instanceId) || !validWidgetId(widget.typeId)) {
            error = QStringLiteral("widgets: instance_id ve type_id, 1–64 ASCII harf, rakam, '-', '_' veya '.' içermeli.");
            return false;
        }
        if (instanceIds.contains(widget.instanceId)) {
            error = QStringLiteral("widgets: instance_id benzersiz olmalı: %1").arg(widget.instanceId);
            return false;
        }
        instanceIds.insert(widget.instanceId);
        if (widget.widthDip < MinimumWidgetWidthDip || widget.widthDip > MaximumWidgetWidthDip) {
            error = QStringLiteral("widgets.width_dip: %1 ile %2 arasında bir tam sayı gerekli.")
                        .arg(MinimumWidgetWidthDip).arg(MaximumWidgetWidthDip);
            return false;
        }
        // Bound the sum of the compact UTF-8 settings objects, including their
        // JSON syntax. Unknown type IDs retain their settings for runtime error
        // cards and future registration; validation is structural only.
        settingsBytes += QJsonDocument(widget.settings).toJson(QJsonDocument::Compact).size();
        if (settingsBytes > MaximumWidgetSettingsBytes) {
            error = QStringLiteral("widgets.settings: toplam JSON boyutu 64 KiB sınırını aşıyor.");
            return false;
        }
    }
    error.clear();
    return true;
}

bool validMonitor(const QString& monitor) {
    if (monitor == QStringLiteral("primary")) return true;
    const QString prefix = QStringLiteral("\\\\.\\DISPLAY");
    if (monitor.size() > 256 || !monitor.startsWith(prefix) || monitor.size() <= prefix.size()) {
        return false;
    }
    const QString number = monitor.mid(prefix.size());
    if (number.front() < QLatin1Char('1') || number.front() > QLatin1Char('9')) return false;
    for (const QChar character : number) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
    }
    return true;
}

bool validate(const PanelSettings& settings, QString& error) {
    if (!validMonitor(settings.monitor)) {
        error = QStringLiteral("monitor: 'primary' veya geçerli bir \\\\.\\DISPLAYn adı gerekli.");
    } else if (settings.side != PanelSide::Left && settings.side != PanelSide::Right) {
        error = QStringLiteral("side: yalnızca 'left' veya 'right' kullanılabilir.");
    } else if (settings.widthDip < 80 || settings.widthDip > 640) {
        error = QStringLiteral("width_dip: 80 ile 640 arasında bir tam sayı gerekli.");
    } else if (settings.offsetDip < 0 || settings.offsetDip > 20000) {
        error = QStringLiteral("offset_dip: 0 ile 20000 arasında bir tam sayı gerekli.");
    } else if (!std::isfinite(settings.opacity) || settings.opacity < 0.25 || settings.opacity > 1.0) {
        error = QStringLiteral("opacity: 0.25 ile 1.0 arasında sonlu bir sayı gerekli.");
    } else {
        return validateWidgets(settings.widgets, error);
    }
    return false;
}

SettingsLoadResult invalidLoad(const QString& error) {
    SettingsLoadResult result;
    result.ok = false;
    result.error = error;
    return result;
}

bool readInteger(const QJsonObject& object, const QString& key, int minimum, int maximum,
                 int& result, QString& error) {
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        const double number = value.toDouble();
        // Check before converting to int; neither fractions nor very large JSON
        // numbers can silently truncate or overflow the settings fields.
        if (std::isfinite(number) && std::trunc(number) == number &&
            number >= minimum && number <= maximum) {
            result = static_cast<int>(number);
            return true;
        }
    }
    error = QStringLiteral("%1: %2 ile %3 arasında bir tam sayı gerekli.")
                .arg(key).arg(minimum).arg(maximum);
    return false;
}

bool readWidgets(const QJsonValue& value, WidgetConfigurations& widgets, QString& error) {
    if (!value.isArray()) {
        error = QStringLiteral("widgets: bir JSON dizisi gerekli.");
        return false;
    }
    const QJsonArray entries = value.toArray();
    if (entries.size() > MaximumWidgetCount) {
        error = QStringLiteral("widgets: en fazla %1 widget kullanılabilir.").arg(MaximumWidgetCount);
        return false;
    }
    WidgetConfigurations parsed;
    parsed.reserve(static_cast<std::size_t>(entries.size()));
    for (const QJsonValue entry : entries) {
        if (!entry.isObject()) {
            error = QStringLiteral("widgets: her öğe bir JSON nesnesi olmalı.");
            return false;
        }
        const QJsonObject object = entry.toObject();
        const QJsonValue instanceId = object.value(QStringLiteral("instance_id"));
        const QJsonValue typeId = object.value(QStringLiteral("type_id"));
        const QJsonValue enabled = object.value(QStringLiteral("enabled"));
        const QJsonValue visible = object.value(QStringLiteral("visible"));
        const QJsonValue settings = object.value(QStringLiteral("settings"));
        if (!instanceId.isString() || !typeId.isString() || !enabled.isBool() ||
            !visible.isBool() || !settings.isObject()) {
            error = QStringLiteral("widgets: kimlikler metin, enabled/visible bool ve settings nesne olmalı.");
            return false;
        }
        WidgetConfiguration widget;
        widget.instanceId = instanceId.toString();
        widget.typeId = typeId.toString();
        widget.enabled = enabled.toBool();
        widget.visible = visible.toBool();
        widget.settings = settings.toObject();
        if (!readInteger(object, QStringLiteral("width_dip"), MinimumWidgetWidthDip,
                         MaximumWidgetWidthDip, widget.widthDip, error)) return false;
        parsed.push_back(std::move(widget));
    }
    if (!validateWidgets(parsed, error)) return false;
    widgets = std::move(parsed);
    return true;
}

QString additionalClockId(const QString& originalId, int ordinal, QSet<QString>& reservedIds) {
    // Reserve every configured ID before allocating split instances, including
    // IDs belonging to later widgets. Truncation keeps 64-character IDs valid.
    for (int suffixNumber = ordinal; ; ++suffixNumber) {
        const QString suffix = QStringLiteral("-%1").arg(suffixNumber);
        const QString candidate = originalId.left(64 - suffix.size()) + suffix;
        if (reservedIds.contains(candidate)) continue;
        reservedIds.insert(candidate);
        return candidate;
    }
}

bool migrateGroupedWorldClocks(WidgetConfigurations& widgets, QString& error) {
    const auto isLegacyGroup = [](const WidgetConfiguration& widget) {
        return widget.typeId == QStringLiteral("world-clock") &&
               (!widget.settings.contains(QStringLiteral("time_zone")) ||
                widget.settings.contains(QStringLiteral("time_zones")));
    };
    std::vector<QJsonArray> zonesByWidget(widgets.size());
    std::size_t expandedCount = 0;
    bool migratedLegacyGroup = false;
    // Validate/count first. A compact legacy list can contain thousands of
    // zones; reject expansion above the limit before allocating widget copies.
    for (std::size_t widgetIndex = 0; widgetIndex < widgets.size(); ++widgetIndex) {
        const auto& widget = widgets[widgetIndex];
        const bool hasSingularZone = widget.settings.contains(QStringLiteral("time_zone"));
        const bool hasZoneList = widget.settings.contains(QStringLiteral("time_zones"));
        if (!isLegacyGroup(widget)) {
            ++expandedCount;
            continue;
        }
        migratedLegacyGroup = true;
        if (hasSingularZone && hasZoneList) {
            error = QStringLiteral("World Clock '%1': 'time_zone' and legacy 'time_zones' cannot both be set.")
                        .arg(widget.instanceId);
            return false;
        }
        QJsonArray zones{QStringLiteral("UTC"), QStringLiteral("America/New_York"),
                         QStringLiteral("Europe/Istanbul")};
        if (hasZoneList) {
            const auto value = widget.settings.value(QStringLiteral("time_zones"));
            if (!value.isArray()) {
                error = QStringLiteral("World Clock '%1': legacy 'time_zones' must be an array.")
                            .arg(widget.instanceId);
                return false;
            }
            zones = value.toArray();
        }
        for (const auto& zone : zones) {
            if (!zone.isString() || zone.toString().isEmpty() ||
                !QTimeZone(zone.toString().toLatin1()).isValid()) {
                error = QStringLiteral("World Clock '%1': legacy 'time_zones' contains an invalid IANA time zone.")
                            .arg(widget.instanceId);
                return false;
            }
        }
        expandedCount += static_cast<std::size_t>(zones.size());
        zonesByWidget[widgetIndex] = std::move(zones);
    }
    if (expandedCount > MaximumWidgetCount) {
        error = QStringLiteral("Splitting legacy World Clocks requires %1 widgets; the limit is %2. "
                               "The original settings file has not been changed.")
                    .arg(static_cast<qulonglong>(expandedCount)).arg(MaximumWidgetCount);
        return false;
    }

    QSet<QString> reservedIds;
    for (const auto& widget : widgets) reservedIds.insert(widget.instanceId);
    WidgetConfigurations migrated;
    migrated.reserve(expandedCount);
    for (std::size_t widgetIndex = 0; widgetIndex < widgets.size(); ++widgetIndex) {
        const auto& widget = widgets[widgetIndex];
        if (!isLegacyGroup(widget)) {
            migrated.push_back(widget);
            continue;
        }
        const auto& zones = zonesByWidget[widgetIndex];
        if (zones.isEmpty()) continue; // An explicitly empty legacy group stays empty.
        const int splitWidth = std::clamp(widget.widthDip / static_cast<int>(zones.size()),
                                         MinimumWidgetWidthDip, MaximumWidgetWidthDip);
        for (qsizetype index = 0; index < zones.size(); ++index) {
            auto clock = widget;
            if (index != 0) clock.instanceId = additionalClockId(widget.instanceId, static_cast<int>(index) + 1,
                                                               reservedIds);
            clock.widthDip = splitWidth;
            clock.settings.remove(QStringLiteral("time_zones"));
            clock.settings.insert(QStringLiteral("time_zone"), zones.at(index));
            migrated.push_back(std::move(clock));
        }
    }
    // Older hosts displayed Countdown first regardless of saved order. Preserve
    // that visual order once during migration; singular configurations use the
    // user's persisted order without further rearrangement.
    if (migratedLegacyGroup) {
        std::stable_partition(migrated.begin(), migrated.end(), [](const auto& widget) {
            return widget.typeId == QStringLiteral("countdown");
        });
    }
    if (!validateWidgets(migrated, error)) return false;
    widgets = std::move(migrated);
    return true;
}

} // namespace

bool PanelSettings::operator==(const PanelSettings& other) const noexcept {
    return monitor == other.monitor && side == other.side && widthDip == other.widthDip &&
           offsetDip == other.offsetDip && opacity == other.opacity &&
           hideForFullscreen == other.hideForFullscreen && widgets == other.widgets;
}

SettingsStore::SettingsStore(QString filePath) : filePath_(std::move(filePath)) {}

QString SettingsStore::defaultPath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(QStringLiteral("settings.json"));
}

QString SettingsStore::filePath() const {
    return filePath_;
}

SettingsLoadResult SettingsStore::load() const {
    if (filePath_.isEmpty()) return invalidLoad(QStringLiteral("Ayar dosyası yolu boş."));
    const QFileInfo info(filePath_);
    if (!info.exists() && !info.isSymLink()) {
        SettingsLoadResult result;
        result.missing = true;
        return result;
    }

    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) {
        return invalidLoad(QStringLiteral("Ayar dosyası açılamadı: ") + file.errorString());
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return invalidLoad(QStringLiteral("Ayar dosyası okunamadı: ") + file.errorString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return invalidLoad(QStringLiteral("Ayar dosyası geçerli JSON değil: ") + parseError.errorString());
    }
    if (!document.isObject()) {
        return invalidLoad(QStringLiteral("Ayar dosyasının kökü bir JSON nesnesi olmalı."));
    }
    const QJsonObject root = document.object();
    int version = 0;
    QString error;
    if (!readInteger(root, QStringLiteral("schema_version"), 1, 2, version, error)) {
        return invalidLoad(QStringLiteral("Desteklenmeyen veya geçersiz ayar sürümü: ") + error);
    }
    if (!root.value(QStringLiteral("panel")).isObject()) {
        return invalidLoad(QStringLiteral("panel: bir JSON nesnesi gerekli."));
    }
    const QJsonObject panel = root.value(QStringLiteral("panel")).toObject();
    PanelSettings settings;
    const QJsonValue monitor = panel.value(QStringLiteral("monitor"));
    const QJsonValue side = panel.value(QStringLiteral("side"));
    const QJsonValue opacity = panel.value(QStringLiteral("opacity"));
    const QJsonValue fullscreen = panel.value(QStringLiteral("hide_for_fullscreen"));
    if (!monitor.isString()) return invalidLoad(QStringLiteral("monitor: bir metin gerekli."));
    if (!side.isString() || (side.toString() != QStringLiteral("left") &&
                             side.toString() != QStringLiteral("right"))) {
        return invalidLoad(QStringLiteral("side: 'left' veya 'right' metni gerekli."));
    }
    if (!opacity.isDouble()) return invalidLoad(QStringLiteral("opacity: bir sayı gerekli."));
    if (!fullscreen.isBool()) return invalidLoad(QStringLiteral("hide_for_fullscreen: bir bool gerekli."));
    settings.monitor = monitor.toString();
    settings.side = side.toString() == QStringLiteral("left") ? PanelSide::Left : PanelSide::Right;
    settings.opacity = opacity.toDouble();
    // Older schema-1 documents may request fullscreen hiding. Accept their
    // boolean field for compatibility, but apply the current visibility policy
    // in memory without rewriting the user's file during load.
    settings.hideForFullscreen = false;
    // Schema one did not define widgets: ignore any such legacy extra key and
    // migrate to the defaults already held in memory, without rewriting bytes.
    if (!readInteger(panel, QStringLiteral("width_dip"), 80, 640, settings.widthDip, error) ||
        !readInteger(panel, QStringLiteral("offset_dip"), 0, 20000, settings.offsetDip, error) ||
        (version == 2 && !readWidgets(root.value(QStringLiteral("widgets")), settings.widgets, error)) ||
        (version == 2 && !migrateGroupedWorldClocks(settings.widgets, error)) ||
        !validate(settings, error)) {
        return invalidLoad(error);
    }
    SettingsLoadResult result;
    result.settings = settings;
    return result;
}

bool SettingsStore::save(const PanelSettings& settings, QString& error) const {
    // Validate first: bad input must not create directories or replace an
    // existing valid/malformed file.
    if (!validate(settings, error)) return false;
    if (filePath_.isEmpty()) {
        error = QStringLiteral("Ayar dosyası yolu boş.");
        return false;
    }
    const QFileInfo info(filePath_);
    if (info.isDir()) {
        error = QStringLiteral("Ayar dosyası yolu bir klasörü gösteriyor.");
        return false;
    }
    if (!QDir().mkpath(info.absolutePath())) {
        error = QStringLiteral("Ayar dosyasının klasörü oluşturulamadı.");
        return false;
    }

    const QJsonObject panel{
        {QStringLiteral("monitor"), settings.monitor},
        {QStringLiteral("side"), settings.side == PanelSide::Left ? QStringLiteral("left") : QStringLiteral("right")},
        {QStringLiteral("width_dip"), settings.widthDip},
        {QStringLiteral("offset_dip"), settings.offsetDip},
        {QStringLiteral("opacity"), settings.opacity},
        {QStringLiteral("hide_for_fullscreen"), false}
    };
    QJsonArray widgets;
    for (const auto& widget : settings.widgets) {
        widgets.append(QJsonObject{
            {QStringLiteral("instance_id"), widget.instanceId},
            {QStringLiteral("type_id"), widget.typeId},
            {QStringLiteral("enabled"), widget.enabled},
            {QStringLiteral("visible"), widget.visible},
            {QStringLiteral("width_dip"), widget.widthDip},
            {QStringLiteral("settings"), widget.settings}
        });
    }
    const QJsonObject root{{QStringLiteral("schema_version"), 2}, {QStringLiteral("panel"), panel},
                           {QStringLiteral("widgets"), widgets}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(filePath_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("Ayar dosyası yazma için açılamadı: ") + file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        error = QStringLiteral("Ayar dosyası tam yazılamadı: ") + file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        error = QStringLiteral("Ayar dosyası atomik olarak kaydedilemedi: ") + file.errorString();
        return false;
    }
    error.clear();
    return true;
}

} // namespace w11widget
