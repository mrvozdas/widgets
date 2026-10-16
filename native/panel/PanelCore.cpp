#include "PanelCore.h"

#include <iomanip>
#include <sstream>

namespace w11widget::native {

std::optional<Options> parseOptions(const std::vector<std::wstring>& arguments, std::wstring& error)
{
    error.clear();
    Options result;
    bool durationSet = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& value = arguments[index];
        if (value == L"--benchmark") result.benchmark = true;
        else if (value == L"--self-test") result.selfTest = true;
        else if (value == L"--smoke-test") result.smokeTest = true;
        else if (value == L"--version") result.version = true;
        else if (value == L"--settings") result.settings = true;
        else if (value == L"--background") result.background = true;
        else if (value == L"--help") result.help = true;
        else if (value == L"--duration-seconds" && !durationSet && index + 1 < arguments.size()) {
            const auto& number = arguments[++index];
            if (number.empty() || number.size() > 5 || number.find_first_not_of(L"0123456789") != std::wstring::npos) {
                error = L"--duration-seconds requires an integer from 1 to 86400.";
                return std::nullopt;
            }
            result.durationSeconds = static_cast<unsigned>(std::stoul(number));
            if (result.durationSeconds < 1 || result.durationSeconds > 86400) {
                error = L"--duration-seconds must be from 1 to 86400.";
                return std::nullopt;
            }
            durationSet = true;
        } else if (value == L"--settings-file" && result.settingsFile.empty() && index + 1 < arguments.size()) {
            result.settingsFile = arguments[++index];
            if (result.settingsFile.empty() || result.settingsFile.find(L'"') != std::wstring::npos) {
                error = L"--settings-file requires an absolute profile path.";
                return std::nullopt;
            }
        } else if (value == L"--metrics-file" && result.metricsFile.empty() && index + 1 < arguments.size()) {
            result.metricsFile = arguments[++index];
            if (result.metricsFile.empty()) {
                error = L"--metrics-file requires an output path.";
                return std::nullopt;
            }
        } else {
            error = L"Unknown, repeated or incomplete option: " + value;
            return std::nullopt;
        }
    }
    if (result.benchmark && (!result.settingsFile.empty() || result.settings || result.background)) {
        error = L"The reference benchmark cannot access a production profile.";
        return std::nullopt;
    }
    if (result.benchmark && !durationSet) result.durationSeconds = 30;
    return result;
}

std::wstring formatClock(int hour, int minute)
{
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return {};
    std::wostringstream text;
    text << std::setfill(L'0') << std::setw(2) << hour << L':' << std::setw(2) << minute;
    return text.str();
}

std::wstring formatRemaining(long long remainingSeconds)
{
    if (remainingSeconds < 0 || remainingSeconds > 172800) return {};
    const long long minutes = (remainingSeconds + 59) / 60;
    std::wostringstream text;
    text << std::setfill(L'0') << std::setw(2) << minutes / 60 << L':' << std::setw(2) << minutes % 60;
    return text.str();
}

unsigned millisecondsToNextMinute(unsigned millisecondsWithinMinute)
{
    return 60000 - millisecondsWithinMinute % 60000 + 25;
}

} // namespace w11widget::native
