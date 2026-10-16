#include "widgets/WorldClockWidget.h"

#include <QTimeZone>
#include <QCursor>
#include <QStringList>
#include <QToolTip>

#include <cstdlib>

namespace w11widget {
namespace {

WorldClockReading readingAt(const QDateTime& utc, const QTimeZone& zone)
{
    const QDateTime local = utc.toTimeZone(zone);
    const int offsetSeconds = local.offsetFromUtc();
    const int absoluteOffset = std::abs(offsetSeconds);
    QString abbreviation = local.timeZoneAbbreviation();
    if (zone.id() == QByteArrayLiteral("America/New_York")) {
        abbreviation = local.isDaylightTime() ? QStringLiteral("EDT") : QStringLiteral("EST");
    } else if (zone.id() == QByteArrayLiteral("Europe/Istanbul")) {
        abbreviation = QStringLiteral("IST");
    }
    return {
        QString::fromLatin1(zone.id()),
        QString::fromLatin1(zone.id()).section(QLatin1Char('/'), -1).replace(QLatin1Char('_'), QLatin1Char(' ')),
        local.date().toString(QStringLiteral("yyyy-MM-dd")),
        local.time().toString(QStringLiteral("HH:mm")),
        QStringLiteral("UTC%1%2:%3")
            .arg(offsetSeconds < 0 ? QLatin1Char('-') : QLatin1Char('+'))
            .arg(absoluteOffset / 3600, 2, 10, QLatin1Char('0'))
            .arg((absoluteOffset % 3600) / 60, 2, 10, QLatin1Char('0')),
        abbreviation
    };
}

QString configuredTimeZone(const QJsonObject& settings)
{
    return settings.value(QStringLiteral("time_zone")).toString(QStringLiteral("UTC"));
}

bool hasOnlyExpectedSettings(const QJsonObject& settings, QString& error)
{
    for (auto item = settings.begin(); item != settings.end(); ++item) {
        if (item.key() != QStringLiteral("show_date") && item.key() != QStringLiteral("time_zone") &&
            item.key() != QStringLiteral("label")) {
            error = QStringLiteral("Unknown World Clock setting '%1'.").arg(item.key());
            return false;
        }
    }
    const auto showDate = settings.value(QStringLiteral("show_date"));
    if (!showDate.isBool()) {
        error = QStringLiteral("World Clock setting 'show_date' must be a boolean.");
        return false;
    }
    const auto label = settings.value(QStringLiteral("label"));
    if (!label.isUndefined() && (!label.isString() || label.toString().size() > 40)) {
        error = QStringLiteral("World Clock setting 'label' must be a string of at most 40 characters.");
        return false;
    }
    const auto configured = settings.value(QStringLiteral("time_zone"));
    if (!configured.isUndefined() && !configured.isString()) {
        error = QStringLiteral("World Clock setting 'time_zone' must be an IANA time zone string.");
        return false;
    }
    const QString zoneId = configuredTimeZone(settings);
    if (zoneId.isEmpty() || !QTimeZone(zoneId.toLatin1()).isValid()) {
        error = QStringLiteral("World Clock setting 'time_zone' has an invalid IANA time zone.");
        return false;
    }
    return true;
}

} // namespace

WorldClockWidget::WorldClockWidget(QObject* parent) : QObject(parent)
{
    refreshTimer_.setParent(this);
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] {
        refreshNow();
        if (active_) scheduleNextRefresh();
    });
}

bool WorldClockWidget::validateSettings(const QJsonObject& settings, QString& error) const
{
    error.clear();
    if (!hasOnlyExpectedSettings(settings, error)) return false;
    return true;
}

bool WorldClockWidget::initialize(const QJsonObject& settings, QString& error)
{
    if (initialized_) {
        error = QStringLiteral("World Clock is already initialized.");
        return false;
    }
    if (!validateSettings(settings, error)) return false;
    initialized_ = true;
    if (!updateSettings(settings, error)) {
        initialized_ = false;
        return false;
    }
    refreshNow();
    return true;
}

bool WorldClockWidget::updateSettings(const QJsonObject& settings, QString& error)
{
    if (!initialized_) {
        error = QStringLiteral("World Clock is not initialized.");
        return false;
    }
    if (!validateSettings(settings, error)) return false;
    const bool showDate = settings.value(QStringLiteral("show_date")).toBool();
    label_ = settings.value(QStringLiteral("label")).toString().trimmed();
    std::vector<WorldClockReading> nextReadings;
    const auto now = QDateTime::currentDateTimeUtc();
    nextReadings.push_back(readingAt(now, QTimeZone(configuredTimeZone(settings).toLatin1())));
    if (showDate_ != showDate) {
        showDate_ = showDate;
        emit showDateChanged();
    }
    clockReadings_ = std::move(nextReadings);
    readings_ = readingsAt(now);
    emit clockChanged();
    return true;
}

bool WorldClockWidget::activate(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Cannot activate an uninitialized World Clock.");
        return false;
    }
    if (!active_) {
        active_ = true;
        refreshNow();
        scheduleNextRefresh();
        emit activeChanged();
    }
    return true;
}

bool WorldClockWidget::suspend(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Cannot suspend an uninitialized World Clock.");
        return false;
    }
    refreshTimer_.stop();
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    return true;
}

void WorldClockWidget::shutdown()
{
    refreshTimer_.stop();
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    initialized_ = false;
}

WorldClockReadings WorldClockWidget::readingsAt(const QDateTime& utc)
{
    const QDateTime normalizedUtc = utc.toUTC();
    return {
        readingAt(normalizedUtc, QTimeZone::utc()),
        readingAt(normalizedUtc, QTimeZone("America/New_York")),
        readingAt(normalizedUtc, QTimeZone("Europe/Istanbul"))
    };
}

bool WorldClockWidget::usesDetailPopupForWidth(int widthDip)
{
    return widthDip < CompactWidthDip;
}

void WorldClockWidget::refreshNow()
{
    if (!initialized_) return;
    const auto now = QDateTime::currentDateTimeUtc();
    for (auto& reading : clockReadings_) {
        reading = readingAt(now, QTimeZone(reading.zoneId.toLatin1()));
    }
    readings_ = readingsAt(now);
    emit clockChanged();
}

QVariantList WorldClockWidget::clocks() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(clockReadings_.size()));
    for (const auto& reading : clockReadings_) {
        result.append(QVariantMap{
            {QStringLiteral("zoneId"), reading.zoneId},
            {QStringLiteral("label"), reading.label},
            {QStringLiteral("date"), reading.date},
            {QStringLiteral("time"), reading.time},
            {QStringLiteral("utcOffset"), reading.utcOffset},
            {QStringLiteral("abbreviation"), reading.abbreviation}
        });
    }
    return result;
}

QString WorldClockWidget::defaultLabelForTimeZone(const QString& zoneId)
{
    if (zoneId == QStringLiteral("Europe/Istanbul")) return QStringLiteral("İstanbul");
    return zoneId.section(QLatin1Char('/'), -1).replace(QLatin1Char('_'), QLatin1Char(' '));
}

QString WorldClockWidget::displayName() const
{
    if (!label_.isEmpty()) return label_;
    return clockReadings_.empty() ? QString{} : defaultLabelForTimeZone(clockReadings_.front().zoneId);
}

QString WorldClockWidget::detailsText() const
{
    const auto detailLine = [](const QString& label, const WorldClockReading& reading) {
        return QStringLiteral("%1 — %2 %3 (%4, %5)")
            .arg(label, reading.date, reading.time, reading.utcOffset, reading.abbreviation);
    };
    QStringList details;
    for (const auto& reading : clockReadings_) {
        details.append(detailLine(displayName(), reading));
    }
    return details.join(QLatin1Char('\n'));
}

void WorldClockWidget::showDetails() const
{
    QToolTip::showText(QCursor::pos(), detailsText());
}

void WorldClockWidget::scheduleNextRefresh()
{
    const QTime time = QDateTime::currentDateTimeUtc().time();
    const int milliseconds = time.msecsSinceStartOfDay();
    refreshTimer_.start(60'000 - (milliseconds % 60'000) + 25);
}

} // namespace w11widget
