#pragma once

#include <QObject>
#include <QRect>

namespace w11widget {

class ScreenGeometry final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QRect availableAt(qreal x, qreal y) const;
};

} // namespace w11widget
