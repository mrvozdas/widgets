#include "widgets/WorldClockWidget.h"

#include <QCoreApplication>
#include <QJsonArray>
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

QDateTime utc(int year, int month, int day, int hour, int minute)
{
    return {QDate(year, month, day), QTime(hour, minute), QTimeZone::utc()};
}

void testTimeZones()
{
    using namespace w11widget;
    const auto midnight = WorldClockWidget::readingsAt(utc(2026, 1, 1, 0, 5));
    check(midnight.utc.date == QStringLiteral("2026-01-01") &&
              midnight.utc.time == QStringLiteral("00:05") &&
              midnight.utc.utcOffset == QStringLiteral("UTC+00:00") &&
              midnight.utc.abbreviation == QStringLiteral("UTC"),
          "UTC renders date, 24-hour time, offset and abbreviation");
    check(midnight.eastern.date == QStringLiteral("2025-12-31") &&
              midnight.eastern.time == QStringLiteral("19:05") &&
              midnight.eastern.utcOffset == QStringLiteral("UTC-05:00") &&
              midnight.eastern.abbreviation == QStringLiteral("EST"),
          "Eastern time crosses the prior-date midnight boundary in standard time");
    check(midnight.turkey.date == QStringLiteral("2026-01-01") &&
              midnight.turkey.time == QStringLiteral("03:05") &&
              midnight.turkey.utcOffset == QStringLiteral("UTC+03:00") &&
              midnight.turkey.abbreviation == QStringLiteral("IST"),
          "Turkey uses IST with the expected local date and offset");
}

void testEasternTransition()
{
    using namespace w11widget;
    const auto before = WorldClockWidget::readingsAt(utc(2026, 3, 8, 6, 59)).eastern;
    const auto after = WorldClockWidget::readingsAt(utc(2026, 3, 8, 7, 1)).eastern;
    check(before.time == QStringLiteral("01:59") && before.utcOffset == QStringLiteral("UTC-05:00") &&
              before.abbreviation == QStringLiteral("EST"),
          "New York is EST immediately before the 2026 DST transition");
    check(after.time == QStringLiteral("03:01") && after.utcOffset == QStringLiteral("UTC-04:00") &&
              after.abbreviation == QStringLiteral("EDT"),
          "New York becomes EDT and skips 02:00 at the 2026 DST transition");
}

void testSettingsAndLifecycle()
{
    using namespace w11widget;
    WorldClockWidget clock;
    QString error;
    check(!clock.validateSettings({}, error) && !error.isEmpty(),
          "World Clock requires an explicit date-display setting");
    check(!clock.validateSettings({{QStringLiteral("show_date"), QStringLiteral("true")}}, error) &&
              !error.isEmpty(),
          "World Clock rejects a nonboolean date-display setting");
    check(clock.initialize({{QStringLiteral("show_date"), false}}, error) && !clock.showDate(),
          "World Clock initializes with dates hidden and defaults to one UTC clock");
    check(clock.clocks().size() == 1 &&
              clock.clocks().first().toMap().value(QStringLiteral("zoneId")) == QStringLiteral("UTC"),
          "omitting the zone cannot create a group of clocks at runtime");
    check(!clock.utcTime().isEmpty() && !clock.easternTime().isEmpty() && !clock.turkeyTime().isEmpty(),
          "legacy clock properties remain available through lazy zone conversion");
    check(clock.preferredWidthDip() == WorldClockWidget::PreferredWidthDip &&
              clock.compactWidthDip() == WorldClockWidget::CompactWidthDip &&
              clock.compactWidthDip() < clock.preferredWidthDip(),
          "World Clock exposes content-derived full and compact width thresholds");
    check(clock.activate(error) && clock.isActive(), "World Clock activates its minute-aligned refresh");
    check(clock.updateSettings({{QStringLiteral("show_date"), true}}, error) && clock.showDate(),
          "World Clock updates the persisted date-display preference");
    check(clock.detailsText().contains(QStringLiteral("UTC")) &&
              !clock.detailsText().contains(QStringLiteral("New York")),
          "World Clock details describe only this instance's zone");
    check(clock.property("details").toString() == clock.detailsText(),
          "the popup consumes a reactive details property without creating a tooltip window");
    check(clock.updateSettings({
              {QStringLiteral("show_date"), true},
              {QStringLiteral("time_zone"), QStringLiteral("Asia/Tokyo")}
          }, error) &&
              clock.clocks().size() == 1 &&
              clock.clocks().first().toMap().value(QStringLiteral("zoneId")) == QStringLiteral("Asia/Tokyo"),
          "World Clock replaces its sole IANA time zone when edited");
    check(!clock.updateSettings({
              {QStringLiteral("show_date"), true},
              {QStringLiteral("time_zone"), QStringLiteral("Invalid/TimeZone")}
          }, error) && !error.isEmpty(),
          "World Clock rejects invalid configured time zones");
    for (const auto& value : QJsonArray{QJsonArray{}, 42, false, QString{}}) {
        check(!clock.validateSettings({{QStringLiteral("show_date"), false},
                                      {QStringLiteral("time_zone"), value}}, error) && !error.isEmpty(),
              "World Clock rejects malformed or empty singular zones");
    }
    check(!clock.validateSettings({{QStringLiteral("show_date"), false},
                                  {QStringLiteral("time_zones"), QJsonArray{QStringLiteral("UTC")}}}, error) &&
              !error.isEmpty(), "runtime rejects unmigrated grouped clock settings");
    check(!clock.validateSettings({{QStringLiteral("show_date"), false},
                                  {QStringLiteral("label"), false}}, error) &&
              !clock.validateSettings({{QStringLiteral("show_date"), false},
                                       {QStringLiteral("label"), QString(41, QLatin1Char('x'))}}, error),
          "clock names reject nontext and overlong values");
    check(clock.clocks().size() == 1 &&
              clock.clocks().first().toMap().value(QStringLiteral("zoneId")) == QStringLiteral("Asia/Tokyo"),
          "failed edits preserve the instance's prior valid reading");
    check(clock.suspend(error) && !clock.isActive(), "World Clock stops refreshing while suspended");
    clock.shutdown();
}

void testIndependentInstances()
{
    using namespace w11widget;
    WorldClockWidget utcClock;
    WorldClockWidget easternClock;
    QString error;
    check(utcClock.initialize({{QStringLiteral("show_date"), false},
                               {QStringLiteral("time_zone"), QStringLiteral("UTC")}}, error) &&
              easternClock.initialize({{QStringLiteral("show_date"), true},
                                       {QStringLiteral("time_zone"), QStringLiteral("America/New_York")}}, error),
          "separate instances initialize with distinct zones and date preferences");
    check(utcClock.activate(error) && easternClock.activate(error) && utcClock.suspend(error) &&
              !utcClock.isActive() && easternClock.isActive(),
          "suspending one clock leaves the other clock active");
    check(utcClock.updateSettings({{QStringLiteral("show_date"), false},
                                   {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")},
                                   {QStringLiteral("label"), QStringLiteral("  Ev Saati  ")}}, error),
          "one clock can change its zone and displayed name independently");
    utcClock.refreshNow();
    easternClock.refreshNow();
    check(utcClock.clocks().size() == 1 && easternClock.clocks().size() == 1 &&
              utcClock.clocks().first().toMap().value(QStringLiteral("zoneId")) == QStringLiteral("Europe/Istanbul") &&
              easternClock.clocks().first().toMap().value(QStringLiteral("zoneId")) == QStringLiteral("America/New_York") &&
              !utcClock.showDate() && easternClock.showDate(),
          "refresh and edits preserve each instance's own sole reading and date preference");
    check(utcClock.displayName() == QStringLiteral("Ev Saati") &&
              utcClock.detailsText().startsWith(QStringLiteral("Ev Saati")) &&
              easternClock.displayName() == QStringLiteral("New York"),
          "minute refresh preserves the trimmed custom clock name and other clocks keep their default names");
    check(utcClock.updateSettings({{QStringLiteral("show_date"), false},
                                   {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")},
                                   {QStringLiteral("label"), QStringLiteral(" \t ")}}, error) &&
              utcClock.displayName() == QStringLiteral("İstanbul"),
          "clearing a custom clock name restores its editable default city name");
    check(utcClock.updateSettings({{QStringLiteral("show_date"), false},
                                   {QStringLiteral("time_zone"), QStringLiteral("Asia/Tokyo")}}, error) &&
              utcClock.displayName() == QStringLiteral("Tokyo") &&
              utcClock.detailsText().startsWith(QStringLiteral("Tokyo")) &&
              easternClock.displayName() == QStringLiteral("New York"),
          "unnamed clocks follow their selected zone's default name independently");
    utcClock.shutdown();
    check(easternClock.isActive(), "shutting down one instance leaves the other active");
    easternClock.shutdown();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    testTimeZones();
    testEasternTransition();
    testSettingsAndLifecycle();
    testIndependentInstances();
    return failures == 0 ? 0 : 1;
}
