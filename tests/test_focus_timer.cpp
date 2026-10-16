#include "widgets/FocusTimerWidget.h"

#include <QCoreApplication>
#include <QTimer>
#include <iostream>

namespace {
int failures = 0;
int checks = 0;
void check(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void testMonotonicSession()
{
    using namespace w11widget;
    qint64 now = 10'000;
    FocusTimerWidget focus(nullptr, [&] { return now; });
    QString error;
    auto settings = FocusTimerWidget::defaultSettings();
    settings.insert(QStringLiteral("duration_minutes"), 1);
    check(focus.initialize(settings, error) && focus.remainingSeconds() == 60 &&
              focus.displayText() == QStringLiteral("01:00"), "focus initializes its configured duration");
    auto* timer = focus.findChild<QTimer*>(QStringLiteral("focusRefreshTimer"));
    check(timer && !timer->isActive(), "an idle focus timer has no scheduled wakeups");
    focus.start();
    check(!focus.isRunning(), "a hidden focus widget cannot start a background session");
    check(focus.activate(error), "focus becomes available when its host is visible");
    focus.start();
    check(focus.isRunning() && timer && timer->isActive(), "only a running session schedules refreshes");
    now += 10'500;
    focus.refreshNow();
    check(focus.remainingSeconds() == 50, "delayed refreshes derive elapsed time from a monotonic deadline");
    focus.pause();
    check(!focus.isRunning() && focus.status() == QStringLiteral("paused") && !timer->isActive(),
          "pausing retains progress and removes all timer wakeups");
    now += 120'000;
    focus.refreshNow();
    check(focus.remainingSeconds() == 50, "paused time is excluded from the session");
    focus.start();
    now += 49'499;
    focus.refreshNow();
    check(focus.remainingSeconds() == 1 && focus.isRunning(), "subsecond progress survives pause and resume");
    ++now;
    focus.refreshNow();
    check(focus.remainingSeconds() == 0 && !focus.isRunning() &&
              focus.status() == QStringLiteral("completed") && !timer->isActive(),
          "completion occurs at the deadline and stops background work");
    focus.start();
    now += 15'000;
    check(focus.suspend(error) && !focus.isActive() && !focus.isRunning() &&
              focus.remainingSeconds() == 45 && !timer->isActive(),
          "hiding the widget explicitly pauses the session at its current progress");
    now += 300'000;
    check(focus.activate(error) && !focus.isRunning() && focus.remainingSeconds() == 45,
          "showing the widget leaves it paused until the user resumes");
    focus.reset();
    check(focus.status() == QStringLiteral("ready") && focus.remainingSeconds() == 60 && !timer->isActive(),
          "reset restores the duration without starting a session");
    focus.shutdown();
    check(!focus.isActive() && !timer->isActive(), "shutdown removes active refreshes");
}

void testSettings()
{
    using namespace w11widget;
    FocusTimerWidget focus;
    QString error;
    check(!focus.validateSettings({}, error) && !error.isEmpty(), "focus rejects missing settings");
    auto settings = FocusTimerWidget::defaultSettings();
    for (const auto& value : {QJsonValue(0), QJsonValue(181), QJsonValue(1.5), QJsonValue(true), QJsonValue("25")}) {
        settings.insert(QStringLiteral("duration_minutes"), value);
        check(!focus.validateSettings(settings, error), "focus rejects malformed or out-of-range durations");
    }
    settings = FocusTimerWidget::defaultSettings();
    check(focus.initialize(settings, error) && focus.activate(error), "default focus settings are usable");
    focus.start();
    settings.insert(QStringLiteral("label"), QStringLiteral("  Yazı  "));
    check(focus.updateSettings(settings, error) && focus.label() == QStringLiteral("Yazı") && focus.isRunning(),
          "renaming preserves an ongoing session");
    settings.insert(QStringLiteral("duration_minutes"), 50);
    check(focus.updateSettings(settings, error) && focus.remainingSeconds() == 3000 && !focus.isRunning(),
          "changing duration resets the session without silently starting it");
    settings.insert(QStringLiteral("label"), QString{});
    check(!focus.updateSettings(settings, error) && focus.label() == QStringLiteral("Yazı"),
          "invalid settings preserve the previous valid state");
}
} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    testMonotonicSession();
    testSettings();
    std::cout << checks << " focus timer checks; " << failures << " failures.\n";
    return failures == 0 ? 0 : 1;
}
