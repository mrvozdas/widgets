#pragma once

#include <QDialog>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace w11widget {

class WidgetHostModel;

class WidgetSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit WidgetSettingsDialog(WidgetHostModel* model, QWidget* parent = nullptr);

signals:
    void backRequested();

public slots:
    void reject() override;

private:
    [[nodiscard]] QString selectedId() const;
    void refreshList(const QString& preferredId = {});
    void refreshEditor();
    void applyChange(const std::function<bool(QString&)>& change,
                     const QString& preferredId = {});
    void applyTextSetting(const QString& key, const QString& value);
    void applyWorldClockSetting(bool showDate);
    void applyWorldClockTimeZone();

    WidgetHostModel* model_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* worldClock_ = nullptr;
    QPushButton* countdown_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QCheckBox* enabled_ = nullptr;
    QCheckBox* visible_ = nullptr;
    QSpinBox* width_ = nullptr;
    QLineEdit* label_ = nullptr;
    QComboBox* accent_ = nullptr;
    QCheckBox* showDate_ = nullptr;
    QWidget* worldClockZoneEditor_ = nullptr;
    QComboBox* timeZone_ = nullptr;
    QPushButton* applyTimeZone_ = nullptr;
    QLabel* error_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    bool refreshing_ = false;
    bool changing_ = false;
};

} // namespace w11widget
