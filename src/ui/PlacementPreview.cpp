#include "ui/PlacementPreview.h"

#include <QPainter>

namespace w11widget {

PlacementPreview::PlacementPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("placementCanvas"));
    setMinimumHeight(110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PlacementPreview::setPlacement(const Rect& taskbar, const Rect& panel)
{
    taskbar_ = taskbar;
    panel_ = panel;
    available_ = taskbar.width() > 0 && taskbar.height() > 0;
    update();
}

void PlacementPreview::clearPlacement()
{
    available_ = false;
    update();
}

QRectF PlacementPreview::projectedPanel() const
{
    if (!available_) return {};
    const qreal availableWidth = qMax(0, width() - 48);
    const qreal scale = availableWidth / taskbar_.width();
    return {24 + (panel_.left - taskbar_.left) * scale, height() / 2.0 - 12,
            panel_.width() * scale, 24};
}

void PlacementPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::Midlight), 1));
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
    const QRectF track(20, height() / 2.0 - 16, qMax(0, width() - 40), 32);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::AlternateBase));
    painter.drawRoundedRect(track, 6, 6);
    if (available_) {
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawRoundedRect(projectedPanel(), 4, 4);
    }
}

} // namespace w11widget
