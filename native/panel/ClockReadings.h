#pragma once

#include <array>
#include <string>

namespace w11widget::native {
struct ClockSnapshot {
    std::array<std::wstring, 4> values;
    bool valid = false;
    std::string error;
};
ClockSnapshot readClocks(double unixMilliseconds);
bool testTimeZones();
} // namespace w11widget::native
