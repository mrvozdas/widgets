#pragma once

#include "core/IWidget.h"

#include <QElapsedTimer>
#include <QTimer>
#include <functional>

namespace w11widget {

class FocusTimerWidget final : public QObject, public IWidget {
    Q_OBJECT
    Q_PROPERTY(QString label READ label NOTIFY timerChanged)
    Q_PROPERTY(int durationMinutes READ durationMinutes NOTIFY timerChanged)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY timerChanged)
    Q_PROPERTY(QString displayText READ displayText NOTIFY timerChanged)
    Q_PROPERTY(QString status READ status NOTIFY timerChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY timerChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)

public:
    using MonotonicClock = std::function<qint64()>;
    explicit FocusTimerWidget(QObject* parent = nullptr, MonotonicClock clock = {});

    bool validateSettings(const QJsonObject& settings, QString& error) const override;
    bool initialize(const QJsonObject& settings, QString& error) override;
    bool updateSettings(const QJsonObject& settings, QString& error) override;
    bool activate(QString& error) override;
    bool suspend(QString& error) override;
    void shutdown() override;
    QObject* viewModel() override { return this; }

    static QJsonObject defaultSettings();
    QString label() const { return label_; }
    int durationMinutes() const { return durationMinutes_; }
    int remainingSeconds() const { return static_cast<int>((remainingMilliseconds_ + 999) / 1000); }
    QString displayText() const;
    QString status() const { return status_; }
    bool isRunning() const { return running_; }
    bool isActive() const { return active_; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void refreshNow();

signals:
    void timerChanged();
    void activeChanged();

private:
    QElapsedTimer elapsed_;
    MonotonicClock clock_;
    QTimer refreshTimer_;
    QString label_ = QStringLiteral("Odak");
    QString status_ = QStringLiteral("ready");
    int durationMinutes_ = 25;
    qint64 remainingMilliseconds_ = 25 * 60 * 1000;
    qint64 deadline_ = 0;
    bool initialized_ = false;
    bool active_ = false;
    bool running_ = false;
};

} // namespace w11widget
