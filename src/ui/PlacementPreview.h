#pragma once

#include "core/PanelPositioner.h"

#include <QWidget>

namespace w11widget {

class PlacementPreview final : public QWidget {
public:
    explicit PlacementPreview(QWidget* parent = nullptr);
    void setPlacement(const Rect& taskbar, const Rect& panel);
    void clearPlacement();
    QRectF projectedPanel() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Rect taskbar_;
    Rect panel_;
    bool available_ = false;
};

} // namespace w11widget
