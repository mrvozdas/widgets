#include "widgets/CountdownWidget.h"

#include <QRegularExpression>
#include <QTimeZone>
#include <QVariantMap>

namespace w11widget {
namespace {

const QString TargetTimeFormat = QStringLiteral("HH:mm");
const QString TargetDateFormat = QStringLiteral("yyyy-MM-dd");

bool readString(const QJsonObject& settings, const QString& key, QString& value, QString& error)
{
    const auto json = settings.value(key);
    if (!json.isString()) {
        error = QStringLiteral("Countdown setting '%1' must be a string.").arg(key);
        return false;
    }
    value = json.toString();
    return true;
}

QDateTime targetDateTime(const QDate& date, const QTime& time, const QTimeZone& zone)
{
    return {date, time, zone};
}

QString remainingText(qint64 remainingSeconds)
{
    const qint64 minutes = (remainingSeconds + 59) / 60;
    return QStringLiteral("⏳ %1:%2")
        .arg(minutes / 60, 2, 10, QLatin1Char('0'))
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

CountdownWidget::CountdownWidget(QObject* parent) : QObject(parent)
{
    refreshTimer_.setParent(this);
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] {
        refreshNow();
        if (active_) scheduleNextRefresh();
    });
}

QJsonObject CountdownWidget::defaultSettings()
{
    return {
        {QStringLiteral("label"), QStringLiteral("Geri sayım")},
        {QStringLiteral("target_time"), QStringLiteral("18:00")},
        {QStringLiteral("daily_repeat"), true},
        {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")},
        {QStringLiteral("target_date"), QString{}}
    };
}

QJsonObject CountdownWidget::normalizedSettings(const QJsonObject& settings,
                                                const QDateTime& nowUtc, QString& error)
{
    error.clear();
    static const QRegularExpression timePattern(QStringLiteral("^\\d{2}:\\d{2}$"));
    for (auto item = settings.begin(); item != settings.end(); ++item) {
        if (item.key() != QStringLiteral("label") && item.key() != QStringLiteral("target_time") &&
            item.key() != QStringLiteral("daily_repeat") && item.key() != QStringLiteral("time_zone") &&
            item.key() != QStringLiteral("target_date")) {
            error = QStringLiteral("Unknown Countdown setting '%1'.").arg(item.key());
            return {};
        }
    }

    QString label;
    QString targetTime;
    QString timeZoneId;
    if (!readString(settings, QStringLiteral("label"), label, error) ||
        !readString(settings, QStringLiteral("target_time"), targetTime, error) ||
        !readString(settings, QStringLiteral("time_zone"), timeZoneId, error)) {
        return {};
    }
    if (label.trimmed().isEmpty() || label.size() > 40) {
        error = QStringLiteral("Countdown label must be a nonblank string of at most 40 characters.");
        return {};
    }
    if (!timePattern.match(targetTime).hasMatch() ||
        !QTime::fromString(targetTime, TargetTimeFormat).isValid()) {
        error = QStringLiteral("Countdown target_time must use valid HH:mm 24-hour time.");
        return {};
    }
    const auto dailyRepeat = settings.value(QStringLiteral("daily_repeat"));
    if (!dailyRepeat.isBool()) {
        error = QStringLiteral("Countdown setting 'daily_repeat' must be a boolean.");
        return {};
    }
    const QTimeZone zone(timeZoneId.toUtf8());
    if (!zone.isValid()) {
        error = QStringLiteral("Countdown time_zone is not an available IANA time zone.");
        return {};
    }

    QString targetDate;
    if (!readString(settings, QStringLiteral("target_date"), targetDate, error)) return {};
    if (!targetDate.isEmpty() && !QDate::fromString(targetDate, TargetDateFormat).isValid()) {
        error = QStringLiteral("Countdown target_date must use valid yyyy-MM-dd date.");
        return {};
    }
    if (!dailyRepeat.toBool() && targetDate.isEmpty()) {
        const QDateTime localNow = nowUtc.toUTC().toTimeZone(zone);
        QDate date = localNow.date();
        const QTime time = QTime::fromString(targetTime, TargetTimeFormat);
        if (targetDateTime(date, time, zone).toUTC() <= nowUtc.toUTC()) date = date.addDays(1);
        targetDate = date.toString(TargetDateFormat);
    }
    return {
        {QStringLiteral("label"), label},
        {QStringLiteral("target_time"), targetTime},
        {QStringLiteral("daily_repeat"), dailyRepeat.toBool()},
        {QStringLiteral("time_zone"), timeZoneId},
        {QStringLiteral("target_date"), targetDate}
    };
}

bool CountdownWidget::validateSettings(const QJsonObject& settings, QString& error) const
{
    return !normalizedSettings(settings, QDateTime::currentDateTimeUtc(), error).isEmpty();
}

bool CountdownWidget::initialize(const QJsonObject& settings, QString& error)
{
    if (initialized_) {
        error = QStringLiteral("Countdown is already initialized.");
        return false;
    }
    initialized_ = true;
    if (!updateSettings(settings, error)) {
        initialized_ = false;
        return false;
    }
    return true;
}

bool CountdownWidget::updateSettings(const QJsonObject& settings, QString& error)
{
    if (!initialized_) {
        error = QStringLiteral("Countdown is not initialized.");
        return false;
    }
    const auto normalized = normalizedSettings(settings, QDateTime::currentDateTimeUtc(), error);
    if (normalized.isEmpty()) return false;
    label_ = normalized.value(QStringLiteral("label")).toString();
    targetTime_ = normalized.value(QStringLiteral("target_time")).toString();
    dailyRepeat_ = normalized.value(QStringLiteral("daily_repeat")).toBool();
    timeZoneId_ = normalized.value(QStringLiteral("time_zone")).toString();
    targetDate_ = normalized.value(QStringLiteral("target_date")).toString();
    refreshNow();
    return true;
}

bool CountdownWidget::activate(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Cannot activate an uninitialized Countdown.");
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

bool CountdownWidget::suspend(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Cannot suspend an uninitialized Countdown.");
        return false;
    }
    refreshTimer_.stop();
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    return true;
}

void CountdownWidget::shutdown()
{
    refreshTimer_.stop();
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    initialized_ = false;
}

CountdownSnapshot CountdownWidget::snapshotAt(const QDateTime& nowUtc, const QJsonObject& settings)
{
    QString error;
    const auto normalized = normalizedSettings(settings, nowUtc, error);
    if (normalized.isEmpty()) return {QStringLiteral("Hata"), QStringLiteral("error"), {}, 0};

    const QTimeZone zone(normalized.value(QStringLiteral("time_zone")).toString().toUtf8());
    const QDateTime normalizedNowUtc = nowUtc.toUTC();
    const QDateTime localNow = normalizedNowUtc.toTimeZone(zone);
    const QTime time = QTime::fromString(normalized.value(QStringLiteral("target_time")).toString(),
                                         TargetTimeFormat);
    const bool dailyRepeat = normalized.value(QStringLiteral("daily_repeat")).toBool();
    QDate date = dailyRepeat ? localNow.date() :
                               QDate::fromString(normalized.value(QStringLiteral("target_date")).toString(),
                                                 TargetDateFormat);
    QDateTime target = targetDateTime(date, time, zone);
    if (!target.isValid()) return {QStringLiteral("Hata"), QStringLiteral("error"), {}, 0};

    qint64 remainingSeconds = normalizedNowUtc.secsTo(target.toUTC());
    if (dailyRepeat && remainingSeconds <= 0) {
        if (-remainingSeconds < CompletionDisplaySeconds) {
            return {QStringLiteral("Tamamlandı"), QStringLiteral("completed"),
                    date.toString(TargetDateFormat), 0};
        }
        date = date.addDays(1);
        target = targetDateTime(date, time, zone);
        remainingSeconds = normalizedNowUtc.secsTo(target.toUTC());
    }
    if (!dailyRepeat && remainingSeconds <= 0) {
        return {QStringLiteral("Tamamlandı"), QStringLiteral("completed"),
                date.toString(TargetDateFormat), 0};
    }
    const qint64 remainingMinutes = (remainingSeconds + 59) / 60;
    return {remainingText(remainingSeconds), QStringLiteral("counting"),
            date.toString(TargetDateFormat), remainingMinutes};
}

bool CountdownWidget::usesCompactDisplayForWidth(int widthDip)
{
    return widthDip < CompactWidthDip;
}

void CountdownWidget::refreshNow()
{
    if (!initialized_) return;
    snapshot_ = snapshotAt(QDateTime::currentDateTimeUtc(), {
        {QStringLiteral("label"), label_},
        {QStringLiteral("target_time"), targetTime_},
        {QStringLiteral("daily_repeat"), dailyRepeat_},
        {QStringLiteral("time_zone"), timeZoneId_},
        {QStringLiteral("target_date"), targetDate_}
    });
    emit countdownChanged();
}

QVariantMap CountdownWidget::normalizedSettingsMap(const QVariantMap& settings)
{
    QString error;
    const auto normalized = normalizedSettings(QJsonObject::fromVariantMap(settings),
                                               QDateTime::currentDateTimeUtc(), error);
    if (lastValidationError_ != error) {
        lastValidationError_ = error;
        emit validationErrorChanged();
    }
    return normalized.toVariantMap();
}

void CountdownWidget::scheduleNextRefresh()
{
    const int milliseconds = QDateTime::currentDateTimeUtc().time().msecsSinceStartOfDay();
    refreshTimer_.start(60'000 - (milliseconds % 60'000) + 25);
}

} // namespace w11widget
