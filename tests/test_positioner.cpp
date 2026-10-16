#include "core/PanelPositioner.h"

#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

void expectBounds(const w11widget::PlacementResult& result, w11widget::Rect expected,
                  const char* name) {
    check(result.ok && result.error.empty() && result.bounds.left == expected.left &&
          result.bounds.top == expected.top && result.bounds.right == expected.right &&
          result.bounds.bottom == expected.bottom, name);
}

void expectRejected(const w11widget::Rect& bar, unsigned dpi, unsigned edge, bool autoHide,
                    const w11widget::PlacementRequest& request, const char* name) {
    const auto result = w11widget::calculatePlacement(bar, dpi, edge, autoHide, request);
    check(!result.ok && !result.error.empty() && !result.bounds.isValid(), name);
}

} // namespace

int main() {
    using namespace w11widget;
    const Rect bottomBar{0, 1032, 1920, 1080};
    PlacementRequest request{PanelSide::Left, 240, 32, 16, true};

    expectBounds(calculatePlacement(bottomBar, 96, 3, false, request),
                 {16, 1040, 256, 1072}, "left 100 percent");
    request.side = PanelSide::Right;
    expectBounds(calculatePlacement(bottomBar, 96, 3, false, request),
                 {1664, 1040, 1904, 1072}, "right offset is measured from right edge");
    expectBounds(calculatePlacement({0, 1290, 2400, 1350}, 120, 3, false, request),
                 {2080, 1300, 2380, 1340}, "125 percent scales size and offset");
    expectBounds(calculatePlacement({0, 1548, 2880, 1620}, 144, 3, false, request),
                 {2496, 1560, 2856, 1608}, "150 percent scales size and offset");
    request.side = PanelSide::Left;
    expectBounds(calculatePlacement({-1920, -48, 0, 0}, 96, 1, false, request),
                 {-1904, -40, -1664, -8}, "negative virtual-screen coordinates and top edge");
    request.side = PanelSide::Right;
    expectBounds(calculatePlacement({-1920, -48, 0, 0}, 96, 1, false, request),
                 {-256, -40, -16, -8}, "right side on negative monitor");
    request = {PanelSide::Left, 3, 3, 1, true};
    expectBounds(calculatePlacement({0, 0, 100, 9}, 144, 1, false, request),
                 {2, 2, 7, 7}, "half pixel sizes round up and vertical centering is deterministic");
    request = {PanelSide::Right, 1920, 48, 0, true};
    expectBounds(calculatePlacement(bottomBar, 96, 3, false, request),
                 bottomBar, "exact fit is accepted without clamping");

    request = {PanelSide::Left, 240, 32, 16, false};
    expectRejected(bottomBar, 96, 3, false, request, "unconfirmed region fails closed");
    request.regionConfirmed = true;
    expectRejected(bottomBar, 96, 3, true, request, "auto-hide yields");
    expectRejected(bottomBar, 0, 3, false, request, "missing DPI");
    expectRejected(bottomBar, 96, 0, false, request, "vertical left taskbar");
    expectRejected(bottomBar, 96, 2, false, request, "vertical right taskbar");
    expectRejected(bottomBar, 96, 4, false, request, "unknown edge");
    expectRejected({}, 96, 3, false, request, "missing geometry");
    expectRejected({10, 10, 0, 0}, 96, 3, false, request, "inverted geometry");

    request.widthDip = 0;
    expectRejected(bottomBar, 96, 3, false, request, "zero width");
    request.widthDip = -1;
    expectRejected(bottomBar, 96, 3, false, request, "negative width");
    request.widthDip = 240;
    request.heightDip = 0;
    expectRejected(bottomBar, 96, 3, false, request, "zero height");
    request.heightDip = -1;
    expectRejected(bottomBar, 96, 3, false, request, "negative height");
    request.heightDip = 49;
    expectRejected(bottomBar, 96, 3, false, request, "excessive height is rejected");
    request.heightDip = 32;
    request.widthDip = 1921;
    expectRejected(bottomBar, 96, 3, false, request, "excessive width is rejected");
    request.widthDip = 240;
    request.offsetDip = -1;
    expectRejected(bottomBar, 96, 3, false, request, "negative offset");
    request.offsetDip = 1681;
    expectRejected(bottomBar, 96, 3, false, request, "offset cannot push panel outside taskbar");
    request.offsetDip = 0;
    request.side = static_cast<PanelSide>(99);
    expectRejected(bottomBar, 96, 3, false, request, "invalid side");

    request = {PanelSide::Left, std::numeric_limits<int>::max(), 32, 0, true};
    expectRejected(bottomBar, std::numeric_limits<unsigned>::max(), 3, false, request,
                   "extreme DPI and size do not overflow");
    request = {PanelSide::Left, 1, 1, std::numeric_limits<int>::max(), true};
    expectRejected(bottomBar, std::numeric_limits<unsigned>::max(), 3, false, request,
                   "extreme offset conversion does not overflow");
    request = {PanelSide::Left, 1, 1, 0, true};
    expectRejected(bottomBar, 1, 3, false, request, "subpixel dimensions rounded to zero fail");
    const Rect hugeBar{std::numeric_limits<int>::min(), 0,
                       std::numeric_limits<int>::max(), 48};
    check(hugeBar.width() == 4294967295LL, "Rect extent avoids signed subtraction overflow");
    expectRejected(hugeBar, 96, 3, false, request, "unrepresentable physical extent");
    const int limit = std::numeric_limits<int>::max();
    expectBounds(calculatePlacement({limit - 100, limit - 48, limit, limit}, 96, 3,
                                    false, {PanelSide::Right, 80, 32, 10, true}),
                 {limit - 90, limit - 40, limit - 10, limit - 8},
                 "placement near positive int coordinate limit");
    const int minimum = std::numeric_limits<int>::min();
    expectBounds(calculatePlacement({minimum, minimum, minimum + 100, minimum + 48},
                                    96, 1, false, {PanelSide::Left, 80, 32, 10, true}),
                 {minimum + 10, minimum + 8, minimum + 90, minimum + 40},
                 "placement near negative int coordinate limit");

    if (failures != 0) {
        std::cerr << failures << " positioning checks failed.\n";
        return 1;
    }
    std::cout << "Positioning checks passed.\n";
    return 0;
}
