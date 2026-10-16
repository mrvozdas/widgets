#pragma once

#include "core/IWidget.h"

#include <QDateTime>
#include <QTimer>
#include <QVariantList>

#include <vector>
#include <optional>

namespace w11widget {

struct WorldClockReading {
    QString zoneId;
    QString label;
    QString date;
    QString time;
    QString utcOffset;
    QString abbreviation;
};

struct WorldClockReadings {
    WorldClockReading utc;
    WorldClockReading eastern;
    WorldClockReading turkey;
};

class WorldClockWidget final : public QObject, public IWidget {
    Q_OBJECT
    Q_PROPERTY(QString utcTime READ utcTime NOTIFY clockChanged)
    Q_PROPERTY(QString easternTime READ easternTime NOTIFY clockChanged)
    Q_PROPERTY(QString turkeyTime READ turkeyTime NOTIFY clockChanged)
    Q_PROPERTY(QString utcDate READ utcDate NOTIFY clockChanged)
    Q_PROPERTY(QString easternDate READ easternDate NOTIFY clockChanged)
    Q_PROPERTY(QString turkeyDate READ turkeyDate NOTIFY clockChanged)
    Q_PROPERTY(QString utcOffset READ utcOffset NOTIFY clockChanged)
    Q_PROPERTY(QString easternOffset READ easternOffset NOTIFY clockChanged)
    Q_PROPERTY(QString turkeyOffset READ turkeyOffset NOTIFY clockChanged)
    Q_PROPERTY(QString utcAbbreviation READ utcAbbreviation NOTIFY clockChanged)
    Q_PROPERTY(QString easternAbbreviation READ easternAbbreviation NOTIFY clockChanged)
    Q_PROPERTY(QString turkeyAbbreviation READ turkeyAbbreviation NOTIFY clockChanged)
    Q_PROPERTY(QVariantList clocks READ clocks NOTIFY clockChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY clockChanged)
    Q_PROPERTY(QString details READ detailsText NOTIFY clockChanged)
    Q_PROPERTY(bool showDate READ showDate NOTIFY showDateChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(int preferredWidthDip READ preferredWidthDip CONSTANT)
    Q_PROPERTY(int compactWidthDip READ compactWidthDip CONSTANT)

public:
    static constexpr int PreferredWidthDip = 72;
    static constexpr int CompactWidthDip = 48;

    explicit WorldClockWidget(QObject* parent = nullptr);

    bool validateSettings(const QJsonObject& settings, QString& error) const override;
    bool initialize(const QJsonObject& settings, QString& error) override;
    bool updateSettings(const QJsonObject& settings, QString& error) override;
    bool activate(QString& error) override;
    bool suspend(QString& error) override;
    void shutdown() override;
    QObject* viewModel() override { return this; }

    [[nodiscard]] QString utcTime() const { return legacyReadings().utc.time; }
    [[nodiscard]] QString easternTime() const { return legacyReadings().eastern.time; }
    [[nodiscard]] QString turkeyTime() const { return legacyReadings().turkey.time; }
    [[nodiscard]] QString utcDate() const { return legacyReadings().utc.date; }
    [[nodiscard]] QString easternDate() const { return legacyReadings().eastern.date; }
    [[nodiscard]] QString turkeyDate() const { return legacyReadings().turkey.date; }
    [[nodiscard]] QString utcOffset() const { return legacyReadings().utc.utcOffset; }
    [[nodiscard]] QString easternOffset() const { return legacyReadings().eastern.utcOffset; }
    [[nodiscard]] QString turkeyOffset() const { return legacyReadings().turkey.utcOffset; }
    [[nodiscard]] QString utcAbbreviation() const { return legacyReadings().utc.abbreviation; }
    [[nodiscard]] QString easternAbbreviation() const { return legacyReadings().eastern.abbreviation; }
    [[nodiscard]] QString turkeyAbbreviation() const { return legacyReadings().turkey.abbreviation; }
    [[nodiscard]] QVariantList clocks() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] bool showDate() const { return showDate_; }
    [[nodiscard]] bool isActive() const { return active_; }
    [[nodiscard]] int preferredWidthDip() const { return PreferredWidthDip; }
    [[nodiscard]] int compactWidthDip() const { return CompactWidthDip; }

    [[nodiscard]] static WorldClockReadings readingsAt(const QDateTime& utc);
    [[nodiscard]] static QString defaultLabelForTimeZone(const QString& zoneId);
    [[nodiscard]] static bool usesDetailPopupForWidth(int widthDip);
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE QString detailsText() const;
    Q_INVOKABLE void showDetails() const;

signals:
    void clockChanged();
    void showDateChanged();
    void activeChanged();

private:
    void scheduleNextRefresh();
    const WorldClockReadings& legacyReadings() const;

    QTimer refreshTimer_;
    QDateTime readingTime_;
    mutable std::optional<WorldClockReadings> legacyReadings_;
    std::vector<WorldClockReading> clockReadings_;
    QString label_;
    bool initialized_ = false;
    bool active_ = false;
    bool showDate_ = false;
};

} // namespace w11widget
