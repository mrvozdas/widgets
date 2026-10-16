#pragma once

#include "core/IWidget.h"

namespace w11widget {

class SampleWidget final : public QObject, public IWidget {
    Q_OBJECT
    Q_PROPERTY(QString label READ label NOTIFY labelChanged)
    Q_PROPERTY(QString accent READ accent NOTIFY accentChanged)
    Q_PROPERTY(int counter READ counter NOTIFY counterChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)

public:
    explicit SampleWidget(QObject* parent = nullptr);
    bool validateSettings(const QJsonObject& settings, QString& error) const override;
    bool initialize(const QJsonObject& settings, QString& error) override;
    bool updateSettings(const QJsonObject& settings, QString& error) override;
    bool activate(QString& error) override;
    bool suspend(QString& error) override;
    void shutdown() override;
    QObject* viewModel() override { return this; }

    QString label() const { return label_; }
    QString accent() const { return accent_; }
    int counter() const { return counter_; }
    bool isActive() const { return active_; }
    Q_INVOKABLE void trigger();

signals:
    void labelChanged();
    void accentChanged();
    void counterChanged();
    void activeChanged();

private:
    QString label_;
    QString accent_;
    int counter_ = 0;
    bool initialized_ = false;
    bool active_ = false;
};

} // namespace w11widget
