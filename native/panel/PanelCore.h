#pragma once

#include <optional>
#include <string>
#include <vector>

namespace w11widget::native {

struct Options {
    bool benchmark = false;
    bool selfTest = false;
    bool help = false;
    unsigned durationSeconds = 0;
    std::wstring metricsFile;
};

std::optional<Options> parseOptions(const std::vector<std::wstring>& arguments, std::wstring& error);
std::wstring formatClock(int hour, int minute);
std::wstring formatRemaining(long long remainingSeconds);
unsigned millisecondsToNextMinute(unsigned millisecondsWithinMinute);

} // namespace w11widget::native
