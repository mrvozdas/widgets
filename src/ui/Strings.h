#pragma once
#include <QObject>
#include <QString>

namespace w11widget {
// Turkish is the source locale. Keys are stable across the QWidget and QML UI.
QString uiText(const char* key);
class Strings final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QString text(const QString& key) const;
};
}
