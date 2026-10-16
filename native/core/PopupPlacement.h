#pragma once
#include "core/PanelPositioner.h"
#include <optional>

namespace w11widget::native {
std::optional<Rect> popupPlacement(const Rect& anchor, const Rect& workArea,
    int widthDip, int heightDip, unsigned dpi);
}
