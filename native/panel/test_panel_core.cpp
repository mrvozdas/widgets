#include "PanelCore.h"
#include "core/PanelPositioner.h"

#include <iostream>

int main()
{
    using namespace w11widget::native;
    unsigned checks = 0;
    unsigned failures = 0;
    const auto check = [&](bool passed, const char* message) {
        ++checks;
        if (!passed) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    std::wstring error;
    auto options = parseOptions({L"--benchmark"}, error);
    check(options && options->durationSeconds == 30, "benchmark defaults to a bounded safe session");
    options = parseOptions({L"--benchmark", L"--duration-seconds", L"300", L"--metrics-file", L"C:\\logs\\ölçüm.json"}, error);
    check(options && options->durationSeconds == 300 && options->metricsFile == L"C:\\logs\\ölçüm.json",
          "explicit benchmark duration and Unicode metrics paths are preserved");
    for (const auto& value : {L"0", L"-1", L"86401", L"1.5", L"99999999999999", L"1;echo"}) {
        check(!parseOptions({L"--duration-seconds", value}, error) && !error.empty(),
              "unsafe or malformed lifetimes are rejected");
    }
    check(!parseOptions({L"--duration-seconds", L"5", L"--duration-seconds", L"10"}, error),
          "ambiguous repeated durations are rejected");
    check(!parseOptions({L"--settings-executable", L"arbitrary.exe"}, error),
          "the prototype cannot launch an arbitrary settings executable via CLI");
    check(formatClock(0, 5) == L"00:05" && formatClock(23, 59) == L"23:59", "24-hour clock boundaries format correctly");
    check(formatClock(24, 0).empty() && formatClock(1, -1).empty(), "invalid clock input is not manufactured into a display");
    check(formatRemaining(1) == L"00:01" && formatRemaining(3601) == L"01:01" && formatRemaining(86400) == L"24:00",
          "countdown rounds positive seconds up to the displayed minute");
    check(millisecondsToNextMinute(0) == 60025 && millisecondsToNextMinute(59999) == 26,
          "clock wakeups align with the next minute without a constant render loop");
    const auto position = w11widget::calculatePlacement({-1920, 1040, 0, 1080}, 96, 3, false,
        {w11widget::PanelSide::Left, 330, 32, 150, true});
    check(position.ok && position.bounds.left == -1770, "existing pure placement logic works on negative monitor coordinates");
    std::cout << checks << " native core checks; " << failures << " failures.\n";
    return failures == 0 ? 0 : 1;
}
