#pragma once

#include <cstdint>
#include <string>

namespace w11widget {

// Physical pixels in the Win32 virtual-screen coordinate space.
struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] constexpr std::int64_t width() const noexcept {
        return static_cast<std::int64_t>(right) - left;
    }
    [[nodiscard]] constexpr std::int64_t height() const noexcept {
        return static_cast<std::int64_t>(bottom) - top;
    }
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return right > left && bottom > top;
    }
};

enum class PanelSide { Left, Right };

struct PlacementRequest {
    PanelSide side = PanelSide::Left;
    int widthDip = 240;
    int heightDip = 32;
    // Distance inward from the selected taskbar edge; negative offsets are unsafe.
    int offsetDip = 0;
    // An explicit user confirmation, never inferred from taskbar geometry alone.
    bool regionConfirmed = false;
};

struct PlacementResult {
    bool ok = false;
    Rect bounds{};
    std::string error;
};

// taskbarEdge uses the Win32 ABE_* numeric values, without coupling this core
// header to windows.h. Only ABE_TOP (1) and ABE_BOTTOM (3) are supported.
[[nodiscard]] PlacementResult calculatePlacement(const Rect& taskbar, unsigned dpi,
                                                unsigned taskbarEdge, bool autoHide,
                                                const PlacementRequest& request);

} // namespace w11widget
