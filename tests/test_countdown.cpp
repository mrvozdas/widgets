#include "widgets/CountdownWidget.h"

#include <QCoreApplication>
#include <QTimeZone>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

QDateTime utc(int year, int month, int day, int hour, int minute, int second = 0)
{
    return {QDate(year, month, day), QTime(hour, minute, second), QTimeZone::utc()};
}

QJsonObject settings(QString targetTime = QStringLiteral("18:00"), bool dailyRepeat = true,
                     QString targetDate = {})
{
    return {
        {QStringLiteral("label"), QStringLiteral("Mesai Bitişi")},
        {QStringLiteral("target_time"), targetTime},
        {QStringLiteral("daily_repeat"), dailyRepeat},
        {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")},
        {QStringLiteral("target_date"), targetDate}
    };
}

void testCountdownStates()
{
    using namespace w11widget;
    const auto beforeTarget = CountdownWidget::snapshotAt(utc(2026, 1, 1, 12, 0), settings());
    check(beforeTarget.status == QStringLiteral("counting") &&
              beforeTarget.displayText == QStringLiteral("⏳ 03:00") &&
              beforeTarget.remainingMinutes == 180,
          "Countdown calculates remaining Istanbul time from the system clock");

    const auto completed = CountdownWidget::snapshotAt(utc(2026, 1, 1, 15, 0, 30), settings());
    check(completed.status == QStringLiteral("completed") &&
              completed.displayText == QStringLiteral("Tamamlandı"),
          "Daily Countdown shows a short completed state at its target");

    const auto nextDay = CountdownWidget::snapshotAt(utc(2026, 1, 1, 15, 1), settings());
    check(nextDay.status == QStringLiteral("counting") &&
              nextDay.targetDate == QStringLiteral("2026-01-02") &&
              nextDay.displayText == QStringLiteral("⏳ 23:59"),
          "Daily Countdown automatically resumes against the next day's target");
}

void testMidnightAndOneShot()
{
    using namespace w11widget;
    const auto midnight = CountdownWidget::snapshotAt(utc(2026, 1, 1, 20, 50),
                                                       settings(QStringLiteral("00:10")));
    check(midnight.status == QStringLiteral("counting") &&
              midnight.targetDate == QStringLiteral("2026-01-02") &&
              midnight.displayText == QStringLiteral("⏳ 00:20"),
          "Daily Countdown resolves a target after Istanbul midnight");

    QString error;
    const auto oneShot = CountdownWidget::normalizedSettings(
        settings(QStringLiteral("18:00"), false), utc(2026, 1, 1, 16, 0), error);
    check(error.isEmpty() && oneShot.value(QStringLiteral("target_date")).toString() ==
              QStringLiteral("2026-01-02"),
          "a past one-shot time persists the automatically resolved next date");
    const auto done = CountdownWidget::snapshotAt(utc(2026, 1, 2, 15, 1), oneShot);
    check(done.status == QStringLiteral("completed") &&
              done.displayText == QStringLiteral("Tamamlandı"),
          "a one-shot Countdown remains completed after its stored target date");
}

void testSettingsAndLifecycle()
{
    using namespace w11widget;
    CountdownWidget countdown;
    QString error;
    check(!countdown.validateSettings({}, error) && !error.isEmpty(),
          "Countdown requires complete settings");
    auto invalid = settings(QStringLiteral("25:00"));
    check(!countdown.validateSettings(invalid, error) && !error.isEmpty(),
          "Countdown rejects invalid target times");
    check(countdown.initialize(settings(), error) &&
              countdown.displayText().startsWith(QStringLiteral("⏳")),
          "Countdown initializes with its default settings");
    check(countdown.activate(error) && countdown.isActive(),
          "Countdown activates its timer");
    check(countdown.preferredWidthDip() == CountdownWidget::PreferredWidthDip &&
              CountdownWidget::usesCompactDisplayForWidth(95) &&
              !CountdownWidget::usesCompactDisplayForWidth(96),
          "Countdown exposes safe full and compact width behavior");
    check(countdown.suspend(error) && !countdown.isActive(),
          "Countdown suspends its timer with the host");
    countdown.shutdown();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    testCountdownStates();
    testMidnightAndOneShot();
    testSettingsAndLifecycle();
    return failures == 0 ? 0 : 1;
}
