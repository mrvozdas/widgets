#include "TimeZones.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace w11widget::native;
namespace {
double instant(int year, unsigned month, unsigned day, int hour, int minute = 0, int second = 0) {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>((sys_days{std::chrono::year{year}/month/day} + hours{hour} + minutes{minute} + seconds{second}).time_since_epoch()).count());
}
}
int main() {
    int checks = 0;
    const auto check = [&](bool condition, const char* name) { ++checks; if (!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); } };
    check(isValidTimeZone(L"UTC") && isValidTimeZone(L"Asia/Kathmandu"), "IANA identifiers");
    check(!isValidTimeZone(L"Somewhere/Invalid") && !isValidTimeZone(L"") && !isValidTimeZone(std::wstring(L"UTC\0junk", 8)), "ICU unknown-zone GMT fallback rejected");
    const auto ny = readWorldClock(L"America/New_York", instant(2026, 1, 1, 0, 5));
    check(ny.valid && ny.time == L"19:05" && ny.date == L"2025-12-31" && ny.offsetSeconds == -18000, "New York prior date");
    const auto ktm = readWorldClock(L"Asia/Kathmandu", instant(2026, 1, 1, 0));
    check(ktm.valid && ktm.time == L"05:45" && ktm.offset == L"UTC+05:45", "quarter-hour offset");
    check(readWorldClock(L"America/New_York", instant(2026, 3, 8, 6, 59)).time == L"01:59" &&
          readWorldClock(L"America/New_York", instant(2026, 3, 8, 7, 1)).time == L"03:01", "DST clock transition");
    CountdownSettings countdown;
    auto reading = readCountdown(countdown, instant(2026, 1, 1, 14, 59));
    check(reading.valid && !reading.completed && reading.remainingSeconds == 60, "daily countdown before target");
    reading = readCountdown(countdown, instant(2026, 1, 1, 15, 0, 59));
    check(reading.valid && reading.completed && reading.targetDate == L"2026-01-01", "completion held for one minute");
    reading = readCountdown(countdown, instant(2026, 1, 1, 15, 1));
    check(reading.valid && !reading.completed && reading.remainingSeconds == 86340 && reading.targetDate == L"2026-01-02", "daily roll forward");
    countdown.dailyRepeat = false; countdown.targetDate = L"2026-01-01";
    reading = readCountdown(countdown, instant(2026, 1, 2, 14));
    check(reading.valid && reading.completed && reading.targetDate == L"2026-01-01", "saved one-shot never rolls");
    countdown.targetDate.clear(); std::wstring error;
    check(normalizeCountdown(countdown, instant(2026, 1, 1, 16), error) && countdown.targetDate == L"2026-01-02", "empty legacy one-shot anchored once");
    check(normalizeCountdown(countdown, instant(2026, 1, 10, 16), error) && countdown.targetDate == L"2026-01-02", "normalization does not move anchored date");
    countdown.dailyRepeat = true; countdown.timeZone = L"America/New_York"; countdown.targetTime = L"02:30"; countdown.targetDate.clear();
    reading = readCountdown(countdown, instant(2026, 3, 8, 6));
    check(reading.valid && reading.remainingSeconds == 5400, "skipped spring time advances by gap");
    reading = readCountdown(countdown, instant(2026, 3, 8, 8));
    check(reading.valid && reading.remainingSeconds == 81000 && reading.targetDate == L"2026-03-09", "tomorrow does not inherit shifted DST hour");
    countdown.targetTime = L"01:30";
    reading = readCountdown(countdown, instant(2026, 11, 1, 4));
    check(reading.valid && reading.remainingSeconds == 5400, "repeated autumn time uses first occurrence");
    countdown.targetTime = L"25:00"; check(!readCountdown(countdown, instant(2026, 1, 1, 0)).valid, "invalid countdown rejected");
    std::cout << "Native OS ICU: " << checks << " checks PASS\n";
}
