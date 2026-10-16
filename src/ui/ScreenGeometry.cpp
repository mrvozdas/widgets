#include "ui/ScreenGeometry.h"

#include <QGuiApplication>
#include <QScreen>
#include <QtMath>

namespace w11widget {

QRect ScreenGeometry::availableAt(qreal x, qreal y) const
{
    auto* screen = QGuiApplication::screenAt(QPoint(qRound(x), qRound(y)));
    if (!screen) screen = QGuiApplication::primaryScreen();
    // A headless platform can expose no screen; keep its test popup bounded.
    return screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
}

} // namespace w11widget
