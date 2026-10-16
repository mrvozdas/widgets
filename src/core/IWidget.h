#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace w11widget {

inline constexpr int WidgetApiVersion = 1;

// Statically linked, UI-thread-only module API. The module owns its view model;
// consumers must not delete it or retain it after shutdown. No C++ DLL ABI is
// promised by this interface.
class IWidget {
public:
    virtual ~IWidget() = default;

    virtual bool validateSettings(const QJsonObject& settings, QString& error) const = 0;
    virtual bool initialize(const QJsonObject& settings, QString& error) = 0;
    virtual bool updateSettings(const QJsonObject& settings, QString& error) = 0;
    virtual bool activate(QString& error) = 0;
    virtual bool suspend(QString& error) = 0;
    virtual void shutdown() = 0;
    virtual QObject* viewModel() = 0;
};

} // namespace w11widget
