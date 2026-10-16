#pragma once
#include "core/PanelPositioner.h"
#include "core/Settings.h"
#include "platform/windows/TaskbarLocator.h"
#include "ui/PanelWindow.h"
#include <QObject>
#include <functional>
#include <memory>
#include <optional>
class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QWidget;
class QScreen;
namespace w11widget {
class WidgetSettingsDialog;
struct ControllerServices {
    std::function<std::optional<windows::TaskbarSnapshot>(std::string&)> readTaskbar;
    std::function<bool(const windows::TaskbarSnapshot&, WId)> fullscreenConflict;
    std::function<bool(const Rect&, QString&)> showPanel;
    std::function<bool(const Rect&, QString&)> retainPanel;
    std::function<std::optional<windows::TaskbarSnapshot>(const QString&, std::string&)> readTaskbarForMonitor;
    std::function<std::vector<windows::MonitorDescription>(std::string&)> monitors;
    QString settingsPath;
    std::function<bool()> trayAvailable;
    std::function<void(QSystemTrayIcon*)> presentTray;
};
class ApplicationController final : public QObject {
    Q_OBJECT
public:
    enum class StartupMode { Panel, Settings, Background };
    explicit ApplicationController(QObject* parent = nullptr, ControllerServices services = {});
    ~ApplicationController() override;
    // Normal startup presents both the panel and its controls.
    bool start(QString& error, bool interactive = true, StartupMode mode = StartupMode::Panel);
public slots:
    void activateControls();
    void activatePanel();
private slots:
    void reevaluateVisibility();
    void handleEnvironmentChange();
    void handlePowerState(bool suspended);
    void handleShellRecreation();
    void savePreferences();
    void resetPreferences();
private:
    void createControls();
    void createTray();
    void refreshPreview();
    void clearConfirmation();
    void updateReadinessStatus();
    void setStatus(const QString& text, bool error = false);
    void populateMonitors();
    void connectScreen(QScreen* screen);
    void applyPreferences(const PanelSettings& settings);
    PanelSettings preferences() const;
    std::optional<windows::TaskbarSnapshot> probeTaskbar(std::string& error) const;
    void hideTemporarily();
    void invalidatePlacement();
    void showPanel();
    void hidePanel();
    void showControls();
    void showDiagnostics();
    void showWidgetSettings();
    PlacementRequest request() const;
    PanelWindow panel_;
    ControllerServices services_;
    bool interactive_ = true;
    bool requestedVisible_ = false;
    bool displayed_ = false;
    bool applyingPreferences_ = false;
    bool preferencesDirty_ = false;
    std::unique_ptr<SettingsStore> settingsStore_;
    QTimer* safetyTimer_ = nullptr;
    QTimer* environmentTimer_ = nullptr;
    QTimer* saveTimer_ = nullptr;
    std::unique_ptr<QDialog> controls_;
    std::unique_ptr<WidgetSettingsDialog> widgetControls_;
    QStackedWidget* controlPages_ = nullptr;
    QWidget* panelControlsPage_ = nullptr;
    std::unique_ptr<QMenu> trayMenu_;
    std::unique_ptr<QSystemTrayIcon> tray_;
    QComboBox* side_ = nullptr;
    QComboBox* monitor_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* offset_ = nullptr;
    QSpinBox* opacity_ = nullptr;
    QCheckBox* confirmed_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* settingsStatus_ = nullptr;
    QPushButton* show_ = nullptr;
    std::optional<windows::TaskbarSnapshot> previewSnapshot_;
    std::optional<windows::TaskbarSnapshot> approvedSnapshot_;
    std::optional<Rect> retainedBounds_;
    PanelSettings activePreferences_;
    QString previewError_;
};
} // namespace w11widget
