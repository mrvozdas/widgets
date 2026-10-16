#include "core/PanelPositioner.h"

#include <limits>

namespace w11widget {
namespace {

PlacementResult failure(const char* message) {
    return {false, {}, message};
}

bool toPhysicalPixels(int dip, unsigned dpi, int& pixels) {
    if (dip < 0 || dpi == 0) {
        return false;
    }

    // All inputs are integral. A 32-bit int times a 32-bit unsigned value,
    // including the rounding bias, fits uint64_t. This is exact and cannot
    // introduce a non-finite value or overflow during intermediate arithmetic.
    static_assert(sizeof(int) <= 4 && sizeof(unsigned) <= 4);
    const auto scaled = static_cast<std::uint64_t>(dip) * dpi;
    const auto rounded = (scaled + 48) / 96;
    if (rounded > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    pixels = static_cast<int>(rounded);
    return true;
}

} // namespace

PlacementResult calculatePlacement(const Rect& taskbar, unsigned dpi,
                                   unsigned taskbarEdge, bool autoHide,
                                   const PlacementRequest& request) {
    if (!request.regionConfirmed) {
        return failure("Confirm an unoccupied taskbar region before showing the panel.");
    }
    if (autoHide) {
        return failure("Panel placement is disabled while taskbar auto-hide is enabled.");
    }
    if (taskbarEdge != 1 && taskbarEdge != 3) {
        return failure("Only a horizontal top or bottom taskbar is supported.");
    }
    if (!taskbar.isValid() || taskbar.width() > std::numeric_limits<int>::max() ||
        taskbar.height() > std::numeric_limits<int>::max()) {
        return failure("The detected taskbar rectangle is invalid or too large.");
    }
    if (request.side != PanelSide::Left && request.side != PanelSide::Right) {
        return failure("The requested panel side is invalid.");
    }
    if (request.widthDip <= 0 || request.heightDip <= 0 || request.offsetDip < 0 || dpi == 0) {
        return failure("Panel size and DPI must be positive; offset must be non-negative.");
    }

    int width = 0;
    int height = 0;
    int offset = 0;
    if (!toPhysicalPixels(request.widthDip, dpi, width) ||
        !toPhysicalPixels(request.heightDip, dpi, height) ||
        !toPhysicalPixels(request.offsetDip, dpi, offset) || width == 0 || height == 0) {
        return failure("DIP conversion produced an empty or overflowing physical-pixel size.");
    }
    if (width > taskbar.width() || height > taskbar.height() ||
        static_cast<std::int64_t>(offset) + width > taskbar.width()) {
        return failure("The requested panel size or offset does not fit inside the taskbar.");
    }

    const std::int64_t left = request.side == PanelSide::Left
        ? static_cast<std::int64_t>(taskbar.left) + offset
        : static_cast<std::int64_t>(taskbar.right) - offset - width;
    const std::int64_t top = static_cast<std::int64_t>(taskbar.top) +
                             (taskbar.height() - height) / 2;
    // Every result is contained by the input rectangle, so its four edges fit
    // int even when the taskbar origin is negative or near an int limit.
    return {true, {static_cast<int>(left), static_cast<int>(top),
                   static_cast<int>(left + width), static_cast<int>(top + height)}, {}};
}

} // namespace w11widget
