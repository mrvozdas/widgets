#pragma once

#include "../core/Configuration.h"
#include <cstdint>

namespace w11widget::native {
struct WorldClockReading {
    bool valid = false;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int offsetSeconds = 0;
    std::wstring date; // yyyy-MM-dd
    std::wstring time; // HH:mm
    std::wstring offset; // UTC+HH:mm
    std::wstring abbreviation;
    std::wstring error;
};
struct CountdownReading {
    bool valid = false;
    bool completed = false;
    std::int64_t remainingSeconds = 0;
    std::wstring targetDate;
    std::wstring error;
};
bool isValidTimeZone(const std::wstring& zone);
WorldClockReading readWorldClock(const std::wstring& zone, double unixMilliseconds);
CountdownReading readCountdown(const CountdownSettings& settings, double unixMilliseconds);
// Resolve an empty one-shot date once before saving; never move a saved one-shot target forward.
bool normalizeCountdown(CountdownSettings& settings, double unixMilliseconds, std::wstring& error);
} // namespace w11widget::native
