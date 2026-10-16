#include "PopupPlacement.h"
#include <algorithm>
#include <limits>

namespace w11widget::native {
std::optional<Rect> popupPlacement(const Rect& anchor, const Rect& work,
    int widthDip, int heightDip, unsigned dpi)
{
    if (!anchor.isValid() || !work.isValid() || widthDip <= 0 || heightDip <= 0 || dpi == 0 ||
        work.width() > std::numeric_limits<int>::max() || work.height() > std::numeric_limits<int>::max()) return {};
    const auto width = std::min(work.width(), (static_cast<std::int64_t>(widthDip) * dpi + 48) / 96);
    const auto height = std::min(work.height(), (static_cast<std::int64_t>(heightDip) * dpi + 48) / 96);
    const auto gap = (8LL * dpi + 48) / 96;
    if (!width || !height) return {};
    const auto left = std::clamp<std::int64_t>(anchor.left, work.left, static_cast<std::int64_t>(work.right) - width);
    std::int64_t top = static_cast<std::int64_t>(anchor.top) - height - gap;
    if (top < work.top) top = static_cast<std::int64_t>(anchor.bottom) + gap;
    top = std::clamp<std::int64_t>(top, work.top, static_cast<std::int64_t>(work.bottom) - height);
    return Rect{static_cast<int>(left),static_cast<int>(top),static_cast<int>(left+width),static_cast<int>(top+height)};
}
}
