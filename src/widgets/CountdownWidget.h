#pragma once

#include "core/IWidget.h"

#include <QDateTime>
#include <QTimer>

namespace w11widget {

struct CountdownSnapshot {
    QString displayText;
    QString status;
    QString targetDate;
    qint64 remainingMinutes = 0;
};

class CountdownWidget final : public QObject, public IWidget {
    Q_OBJECT
    Q_PROPERTY(QString label READ label NOTIFY countdownChanged)
    Q_PROPERTY(QString targetTime READ targetTime NOTIFY countdownChanged)
    Q_PROPERTY(bool dailyRepeat READ dailyRepeat NOTIFY countdownChanged)
    Q_PROPERTY(QString timeZoneId READ timeZoneId NOTIFY countdownChanged)
    Q_PROPERTY(QString targetDate READ targetDate NOTIFY countdownChanged)
    Q_PROPERTY(QString displayText READ displayText NOTIFY countdownChanged)
    Q_PROPERTY(QString status READ status NOTIFY countdownChanged)
    Q_PROPERTY(QString lastValidationError READ lastValidationError NOTIFY validationErrorChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(int preferredWidthDip READ preferredWidthDip CONSTANT)
    Q_PROPERTY(int compactWidthDip READ compactWidthDip CONSTANT)

public:
    static constexpr int PreferredWidthDip = 72;
    static constexpr int CompactWidthDip = 96;
    static constexpr int CompletionDisplaySeconds = 60;

    explicit CountdownWidget(QObject* parent = nullptr);

    bool validateSettings(const QJsonObject& settings, QString& error) const override;
    bool initialize(const QJsonObject& settings, QString& error) override;
    bool updateSettings(const QJsonObject& settings, QString& error) override;
    bool activate(QString& error) override;
    bool suspend(QString& error) override;
    void shutdown() override;
    QObject* viewModel() override { return this; }

    [[nodiscard]] QString label() const { return label_; }
    [[nodiscard]] QString targetTime() const { return targetTime_; }
    [[nodiscard]] bool dailyRepeat() const { return dailyRepeat_; }
    [[nodiscard]] QString timeZoneId() const { return timeZoneId_; }
    [[nodiscard]] QString targetDate() const { return targetDate_; }
    [[nodiscard]] QString displayText() const { return snapshot_.displayText; }
    [[nodiscard]] QString status() const { return snapshot_.status; }
    [[nodiscard]] QString lastValidationError() const { return lastValidationError_; }
    [[nodiscard]] bool isActive() const { return active_; }
    [[nodiscard]] int preferredWidthDip() const { return PreferredWidthDip; }
    [[nodiscard]] int compactWidthDip() const { return CompactWidthDip; }

    [[nodiscard]] static QJsonObject defaultSettings();
    [[nodiscard]] static QJsonObject normalizedSettings(const QJsonObject& settings,
                                                         const QDateTime& nowUtc, QString& error);
    [[nodiscard]] static CountdownSnapshot snapshotAt(const QDateTime& nowUtc,
                                                       const QJsonObject& settings);
    [[nodiscard]] static bool usesCompactDisplayForWidth(int widthDip);

    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE QVariantMap normalizedSettingsMap(const QVariantMap& settings);

signals:
    void countdownChanged();
    void activeChanged();
    void validationErrorChanged();

private:
    void scheduleNextRefresh();

    QTimer refreshTimer_;
    CountdownSnapshot snapshot_;
    QString label_;
    QString targetTime_;
    QString timeZoneId_;
    QString targetDate_;
    bool dailyRepeat_ = true;
    bool initialized_ = false;
    bool active_ = false;
    QString lastValidationError_;
};

} // namespace w11widget
