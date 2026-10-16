#include "TimeZones.h"
#include <icu.h>
#include <cmath>
#include <cstdio>
#include <memory>

namespace w11widget::native {
namespace {
using Calendar = std::unique_ptr<UCalendar, decltype(&ucal_close)>;
const UChar* utf16(const std::wstring& text) {
    static_assert(sizeof(wchar_t) == sizeof(UChar));
    return reinterpret_cast<const UChar*>(text.c_str());
}
Calendar calendarAt(const std::wstring& zone, double now, UErrorCode& status) {
    Calendar calendar(ucal_open(utf16(zone), static_cast<int32_t>(zone.size()), "tr_TR", UCAL_GREGORIAN, &status), ucal_close);
    if (calendar && U_SUCCESS(status)) {
        // Match Qt's earlier occurrence in autumn and forward adjustment of a skipped spring time.
        ucal_setAttribute(calendar.get(), UCAL_REPEATED_WALL_TIME, UCAL_WALLTIME_FIRST);
        ucal_setAttribute(calendar.get(), UCAL_SKIPPED_WALL_TIME, UCAL_WALLTIME_LAST);
        ucal_setMillis(calendar.get(), now, &status);
    }
    return calendar;
}
std::wstring dateText(UCalendar* calendar, UErrorCode& status) {
    wchar_t text[32]{};
    const auto year = ucal_get(calendar, UCAL_YEAR, &status);
    const auto month = ucal_get(calendar, UCAL_MONTH, &status) + 1;
    const auto day = ucal_get(calendar, UCAL_DATE, &status);
    swprintf(text, 32, L"%04d-%02d-%02d", year, month, day); return text;
}
bool validInput(const CountdownSettings& settings, double now) {
    return std::isfinite(now) && isValidTimeZone(settings.timeZone) && validTime(settings.targetTime) &&
        (settings.targetDate.empty() || validDate(settings.targetDate));
}
void setTarget(UCalendar* calendar, const CountdownSettings& settings, UErrorCode& status) {
    if (!settings.dailyRepeat && !settings.targetDate.empty()) {
        ucal_setDate(calendar, std::stoi(settings.targetDate.substr(0, 4)),
                     std::stoi(settings.targetDate.substr(5, 2)) - 1, std::stoi(settings.targetDate.substr(8, 2)), &status);
    }
    ucal_set(calendar, UCAL_HOUR_OF_DAY, std::stoi(settings.targetTime.substr(0, 2)));
    ucal_set(calendar, UCAL_MINUTE, std::stoi(settings.targetTime.substr(3, 2)));
    ucal_set(calendar, UCAL_SECOND, 0); ucal_set(calendar, UCAL_MILLISECOND, 0);
}
}
bool isValidTimeZone(const std::wstring& zone) {
    if (zone.empty() || zone.size() > 256 || zone.find(L'\0') != std::wstring::npos) return false;
    UErrorCode status = U_ZERO_ERROR; UBool systemId = false; UChar canonical[512]{};
    const auto length = ucal_getCanonicalTimeZoneID(utf16(zone), static_cast<int32_t>(zone.size()), canonical, 512, &systemId, &status);
    return U_SUCCESS(status) && systemId && length > 0;
}
WorldClockReading readWorldClock(const std::wstring& zone, double now) {
    WorldClockReading result;
    if (!std::isfinite(now) || !isValidTimeZone(zone)) { result.error = configText::invalidZone; return result; }
    UErrorCode status = U_ZERO_ERROR; auto calendar = calendarAt(zone, now, status);
    if (!calendar || U_FAILURE(status)) { result.error = configText::timeError; return result; }
    result.year = ucal_get(calendar.get(), UCAL_YEAR, &status);
    result.month = ucal_get(calendar.get(), UCAL_MONTH, &status) + 1;
    result.day = ucal_get(calendar.get(), UCAL_DATE, &status);
    result.hour = ucal_get(calendar.get(), UCAL_HOUR_OF_DAY, &status);
    result.minute = ucal_get(calendar.get(), UCAL_MINUTE, &status);
    result.second = ucal_get(calendar.get(), UCAL_SECOND, &status);
    const auto dst = ucal_get(calendar.get(), UCAL_DST_OFFSET, &status);
    result.offsetSeconds = (ucal_get(calendar.get(), UCAL_ZONE_OFFSET, &status) + dst) / 1000;
    result.date = dateText(calendar.get(), status);
    wchar_t text[32]{}; swprintf(text, 32, L"%02d:%02d", result.hour, result.minute); result.time = text;
    const int offset = std::abs(result.offsetSeconds);
    swprintf(text, 32, L"UTC%c%02d:%02d", result.offsetSeconds < 0 ? L'-' : L'+', offset / 3600, offset % 3600 / 60); result.offset = text;
    UChar abbreviation[128]{};
    const auto size = ucal_getTimeZoneDisplayName(calendar.get(), dst ? UCAL_SHORT_DST : UCAL_SHORT_STANDARD,
                                                 "en_US", abbreviation, 128, &status);
    if (U_SUCCESS(status)) result.abbreviation.assign(reinterpret_cast<const wchar_t*>(abbreviation), static_cast<std::size_t>(size));
    result.valid = U_SUCCESS(status);
    if (!result.valid) result.error = configText::timeError;
    return result;
}
bool normalizeCountdown(CountdownSettings& settings, double now, std::wstring& error) {
    error.clear();
    if (!validInput(settings, now)) { error = configText::timeError; return false; }
    if (settings.dailyRepeat || !settings.targetDate.empty()) return true;
    UErrorCode status = U_ZERO_ERROR; auto calendar = calendarAt(settings.timeZone, now, status);
    if (!calendar || U_FAILURE(status)) { error = configText::timeError; return false; }
    setTarget(calendar.get(), settings, status);
    if (ucal_getMillis(calendar.get(), &status) <= now) ucal_add(calendar.get(), UCAL_DATE, 1, &status);
    settings.targetDate = dateText(calendar.get(), status);
    if (U_FAILURE(status)) { error = configText::timeError; return false; }
    return true;
}
CountdownReading readCountdown(const CountdownSettings& input, double now) {
    CountdownReading result; auto settings = input;
    if (!normalizeCountdown(settings, now, result.error)) return result;
    UErrorCode status = U_ZERO_ERROR; auto calendar = calendarAt(settings.timeZone, now, status);
    if (!calendar || U_FAILURE(status)) { result.error = configText::timeError; return result; }
    setTarget(calendar.get(), settings, status);
    double remaining = ucal_getMillis(calendar.get(), &status) - now;
    if (settings.dailyRepeat && remaining <= -60000) {
        ucal_add(calendar.get(), UCAL_DATE, 1, &status);
        // Reapply wall time after a DST-normalized target so tomorrow does not inherit its shifted hour.
        setTarget(calendar.get(), settings, status);
        remaining = ucal_getMillis(calendar.get(), &status) - now;
    }
    result.targetDate = dateText(calendar.get(), status);
    result.completed = remaining <= 0;
    result.remainingSeconds = remaining > 0 ? static_cast<std::int64_t>(std::ceil(remaining / 1000)) : 0;
    result.valid = U_SUCCESS(status);
    if (!result.valid) result.error = configText::timeError;
    return result;
}
}
