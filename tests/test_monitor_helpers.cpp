#include "platform/windows/TaskbarLocator.h"
#include <shellapi.h>

#include <iostream>

int main() {
    using w11widget::Rect;
    using w11widget::windows::observedHorizontalEdge;
    int failures = 0;
    const auto check = [&](bool passed, const char* description) {
        if (!passed) {
            ++failures;
            std::cerr << "[FAIL] " << description << '\n';
        }
    };
    const Rect primary{0, 0, 1920, 1080};
    check(observedHorizontalEdge({0, 1032, 1920, 1080}, primary) == static_cast<unsigned>(ABE_BOTTOM),
          "bottom horizontal taskbar");
    check(observedHorizontalEdge({0, 0, 1920, 48}, primary) == static_cast<unsigned>(ABE_TOP),
          "top horizontal taskbar");
    const Rect negative{-1920, -200, 0, 880};
    check(observedHorizontalEdge({-1920, 832, 0, 880}, negative) == static_cast<unsigned>(ABE_BOTTOM),
          "secondary negative monitor coordinates");
    check(!observedHorizontalEdge({0, 48, 1920, 96}, primary),
          "inset window has no observed taskbar edge");
    check(!observedHorizontalEdge({40, 1032, 1880, 1080}, primary),
          "partial-width window cannot imply an edge");
    check(!observedHorizontalEdge({0, 0, 48, 1080}, primary),
          "vertical taskbar is unsupported");
    check(!observedHorizontalEdge(primary, primary),
          "whole-monitor rectangle is ambiguous");
    check(!observedHorizontalEdge({-1, 1032, 1920, 1080}, primary),
          "bounds outside the observed monitor are refused");
    check(!observedHorizontalEdge({0, 0, 0, 0}, primary),
          "empty taskbar is refused");
    check(!observedHorizontalEdge({0, 0, 1920, 48}, {}),
          "empty monitor is refused");
    std::cout << "MONITOR HELPER TEST: " << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
