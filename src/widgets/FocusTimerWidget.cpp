#include "widgets/FocusTimerWidget.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace w11widget {

FocusTimerWidget::FocusTimerWidget(QObject* parent, MonotonicClock clock)
    : QObject(parent), clock_(std::move(clock))
{
    elapsed_.start();
    if (!clock_) clock_ = [this] { return elapsed_.elapsed(); };
    refreshTimer_.setParent(this);
    refreshTimer_.setObjectName(QStringLiteral("focusRefreshTimer"));
    refreshTimer_.setInterval(1000);
    refreshTimer_.setTimerType(Qt::CoarseTimer);
    connect(&refreshTimer_, &QTimer::timeout, this, &FocusTimerWidget::refreshNow);
}

QJsonObject FocusTimerWidget::defaultSettings()
{
    return {{QStringLiteral("label"), QStringLiteral("Odak")},
            {QStringLiteral("duration_minutes"), 25}};
}

bool FocusTimerWidget::validateSettings(const QJsonObject& settings, QString& error) const
{
    error.clear();
    for (auto item = settings.begin(); item != settings.end(); ++item) {
        if (item.key() != QStringLiteral("label") && item.key() != QStringLiteral("duration_minutes")) {
            error = QStringLiteral("Bilinmeyen odak sayacı ayarı: %1").arg(item.key());
            return false;
        }
    }
    const auto label = settings.value(QStringLiteral("label"));
    if (!label.isString() || label.toString().trimmed().isEmpty() || label.toString().size() > 40) {
        error = QStringLiteral("Odak adı 1–40 karakter içermeli.");
        return false;
    }
    const auto minutes = settings.value(QStringLiteral("duration_minutes"));
    const double number = minutes.toDouble();
    if (!minutes.isDouble() || !std::isfinite(number) || number < 1 || number > 180 ||
        std::floor(number) != number) {
        error = QStringLiteral("Odak süresi 1–180 dakika arasında bir tam sayı olmalı.");
        return false;
    }
    return true;
}

bool FocusTimerWidget::initialize(const QJsonObject& settings, QString& error)
{
    if (initialized_) {
        error = QStringLiteral("Odak sayacı zaten hazır.");
        return false;
    }
    if (!validateSettings(settings, error)) return false;
    initialized_ = true;
    return updateSettings(settings, error);
}

bool FocusTimerWidget::updateSettings(const QJsonObject& settings, QString& error)
{
    if (!initialized_) {
        error = QStringLiteral("Odak sayacı henüz hazır değil.");
        return false;
    }
    if (!validateSettings(settings, error)) return false;
    label_ = settings.value(QStringLiteral("label")).toString().trimmed();
    const int duration = settings.value(QStringLiteral("duration_minutes")).toInt();
    if (durationMinutes_ != duration) {
        durationMinutes_ = duration;
        reset();
    } else {
        emit timerChanged();
    }
    return true;
}

bool FocusTimerWidget::activate(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Odak sayacı henüz hazır değil.");
        return false;
    }
    if (!active_) {
        active_ = true;
        emit activeChanged();
    }
    return true;
}

bool FocusTimerWidget::suspend(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Odak sayacı henüz hazır değil.");
        return false;
    }
    // Hiding the widget pauses the session; showing it never resumes silently.
    pause();
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    return true;
}

void FocusTimerWidget::shutdown()
{
    reset();
    refreshTimer_.stop();
    initialized_ = false;
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
}

QString FocusTimerWidget::displayText() const
{
    const int seconds = remainingSeconds();
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

void FocusTimerWidget::start()
{
    if (!initialized_ || !active_ || running_) return;
    if (remainingMilliseconds_ == 0) remainingMilliseconds_ = durationMinutes_ * 60 * 1000;
    deadline_ = clock_() + remainingMilliseconds_;
    running_ = true;
    status_ = QStringLiteral("running");
    refreshTimer_.start();
    emit timerChanged();
}

void FocusTimerWidget::pause()
{
    if (!running_) return;
    refreshNow();
    if (!running_) return;
    running_ = false;
    status_ = QStringLiteral("paused");
    refreshTimer_.stop();
    emit timerChanged();
}

void FocusTimerWidget::reset()
{
    if (!initialized_) return;
    refreshTimer_.stop();
    running_ = false;
    remainingMilliseconds_ = durationMinutes_ * 60 * 1000;
    status_ = QStringLiteral("ready");
    emit timerChanged();
}

void FocusTimerWidget::refreshNow()
{
    if (!running_) return;
    const int previousSeconds = remainingSeconds();
    remainingMilliseconds_ = std::max<qint64>(0, deadline_ - clock_());
    if (remainingMilliseconds_ == 0) {
        running_ = false;
        status_ = QStringLiteral("completed");
        refreshTimer_.stop();
    }
    if (remainingSeconds() != previousSeconds || !running_) emit timerChanged();
}

} // namespace w11widget
