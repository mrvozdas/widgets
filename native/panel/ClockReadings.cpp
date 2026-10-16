#include "ClockReadings.h"
#include "PanelCore.h"
#include "locales/tr.h"

#include <icu.h>
#include <iostream>
#include <memory>

namespace w11widget::native {
namespace {
using Calendar = std::unique_ptr<UCalendar, decltype(&ucal_close)>;

Calendar openCalendar(const char16_t* zone, double instant, UErrorCode& error)
{
    Calendar calendar(ucal_open(zone, -1, "tr_TR", UCAL_GREGORIAN, &error), &ucal_close);
    if (calendar && U_SUCCESS(error)) ucal_setMillis(calendar.get(), instant, &error);
    return calendar;
}

std::wstring timeText(UCalendar* calendar, UErrorCode& error)
{
    const int hour = ucal_get(calendar, UCAL_HOUR_OF_DAY, &error);
    const int minute = ucal_get(calendar, UCAL_MINUTE, &error);
    return U_SUCCESS(error) ? formatClock(hour, minute) : std::wstring{};
}
} // namespace

ClockSnapshot readClocks(double unixMilliseconds)
{
    ClockSnapshot snapshot;
    UErrorCode error = U_ZERO_ERROR;
    auto utc = openCalendar(u"UTC", unixMilliseconds, error);
    auto newYork = openCalendar(u"America/New_York", unixMilliseconds, error);
    auto istanbul = openCalendar(u"Europe/Istanbul", unixMilliseconds, error);
    if (!utc || !newYork || !istanbul || U_FAILURE(error)) {
        snapshot.error = "ICU calendar initialization failed: " + std::to_string(static_cast<int>(error));
        return snapshot;
    }
    snapshot.values[1] = timeText(utc.get(), error);
    snapshot.values[2] = timeText(newYork.get(), error);
    snapshot.values[3] = timeText(istanbul.get(), error);
    ucal_set(istanbul.get(), UCAL_HOUR_OF_DAY, 18);
    ucal_set(istanbul.get(), UCAL_MINUTE, 0);
    ucal_set(istanbul.get(), UCAL_SECOND, 0);
    ucal_set(istanbul.get(), UCAL_MILLISECOND, 0);
    double remaining = ucal_getMillis(istanbul.get(), &error) - unixMilliseconds;
    if (remaining <= 0 && remaining > -60000) snapshot.values[0] = strings::Completed;
    else {
        if (remaining <= 0) {
            ucal_add(istanbul.get(), UCAL_DATE, 1, &error);
            remaining = ucal_getMillis(istanbul.get(), &error) - unixMilliseconds;
        }
        snapshot.values[0] = formatRemaining(static_cast<long long>((remaining + 999) / 1000));
    }
    snapshot.valid = U_SUCCESS(error);
    for (const auto& value : snapshot.values) snapshot.valid = snapshot.valid && !value.empty();
    if (!snapshot.valid) snapshot.error = "ICU time conversion failed: " + std::to_string(static_cast<int>(error));
    return snapshot;
}

bool testTimeZones()
{
    // Fixed UTC instants verify rollover, Istanbul and New York DST using OS ICU.
    const auto january = readClocks(1767225900000.0); // 2026-01-01 00:05 UTC
    const auto beforeDst = readClocks(1772953140000.0); // 2026-03-08 06:59 UTC
    const auto afterDst = readClocks(1772953260000.0); // 2026-03-08 07:01 UTC
    const bool passed = january.valid && january.values[1] == L"00:05" &&
        january.values[2] == L"19:05" && january.values[3] == L"03:05" &&
        beforeDst.valid && beforeDst.values[2] == L"01:59" &&
        afterDst.valid && afterDst.values[2] == L"03:01";
    std::cout << "Native OS ICU timezone checks: " << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
} // namespace w11widget::native
