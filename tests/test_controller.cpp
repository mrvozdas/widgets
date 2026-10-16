#include "app/ApplicationController.h"
#include "core/WidgetHostModel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void polishItemTree(QQuickItem* item) {
    item->ensurePolished();
    for (QQuickItem* child : item->childItems()) polishItemTree(child);
}

void settlePanelLayout(QQuickItem* root) {
    // Hidden windows do not render frames, so explicitly run the real QML
    // positioners and the deferred layout callbacks without showing a window.
    for (int pass = 0; pass < 4; ++pass) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        polishItemTree(root);
    }
}

QRectF panelBounds(QQuickItem* item, QQuickItem* root) {
    return item->mapRectToItem(root, item->boundingRect());
}

QList<QQuickItem*> panelItems(QQuickItem* root, const QString& name) {
    // Loader/Repeater items belong to the visual tree; their QObject ownership
    // is not required to follow the same hierarchy.
    QList<QQuickItem*> result;
    if (root->objectName() == name) result.push_back(root);
    for (QQuickItem* child : root->childItems()) result.append(panelItems(child, name));
    return result;
}

QQuickItem* panelItem(QQuickItem* root, const QString& name) {
    const auto matches = panelItems(root, name);
    return matches.isEmpty() ? nullptr : matches.front();
}

QQuickItem* clockItem(QQuickItem* root, const QString& instanceId) {
    for (QQuickItem* item : panelItems(root, QStringLiteral("worldClockWidget"))) {
        if (item->property("widgetInstanceId").toString() == instanceId) return item;
    }
    return nullptr;
}

int widgetRow(w11widget::WidgetHostModel* host, const QString& instanceId) {
    for (int row = 0; row < host->rowCount(); ++row) {
        if (host->data(host->index(row, 0), w11widget::WidgetHostModel::InstanceIdRole).toString() == instanceId)
            return row;
    }
    return -1;
}

QObject* widgetModel(w11widget::WidgetHostModel* host, const QString& instanceId) {
    const int row = widgetRow(host, instanceId);
    return row < 0 ? nullptr
                  : host->data(host->index(row, 0), w11widget::WidgetHostModel::ViewModelRole).value<QObject*>();
}

void checkIndependentPanelLayout(QQuickItem* root, int countdownCount, int clockCount) {
    settlePanelLayout(root);
    auto* viewport = panelItem(root, QStringLiteral("panelWidgetViewport"));
    auto* settings = panelItem(root, QStringLiteral("worldClockSettingsButton"));
    if (!viewport || !settings) {
        check(false, "independent layout exposes its actual viewport and global Settings button");
        return;
    }
    const QRectF viewportBounds = panelBounds(viewport, root);
    const QRectF settingsBounds = panelBounds(settings, root);
    check(panelItems(root, QStringLiteral("worldClockSettingsButton")).size() == 1 &&
              root->boundingRect().contains(settingsBounds) &&
              viewportBounds.right() <= settingsBounds.left() + 0.5,
          "one fully visible Settings button remains outside the scrolling widget viewport");
    check(viewport->property("contentWidth").toReal() <= viewport->width() + 0.5 ||
              viewport->property("interactive").toBool(),
          "content that exceeds the saved panel width can scroll horizontally");

    const auto countdowns = panelItems(root, QStringLiteral("countdownWidget"));
    check(countdowns.size() == countdownCount, "layout contains exactly the visible countdown instances");
    for (QQuickItem* countdown : countdowns) {
        auto* display = panelItem(countdown, QStringLiteral("countdownDisplay"));
        if (!display) {
            check(false, "Countdown exposes its rendered display text");
            continue;
        }
        const QRectF countdownBounds = panelBounds(countdown, root);
        check(std::abs(countdown->width() - countdown->implicitWidth()) <= 1.0 &&
                  countdown->width() <= display->implicitWidth() + 16.0 &&
                  countdownBounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(panelBounds(display, root)),
              "Countdown uses only its measured icon and text width with small padding and fits its text");
    }

    const auto clocks = panelItems(root, QStringLiteral("worldClockWidget"));
    check(clocks.size() == clockCount, "layout creates a separate widget for each visible clock");
    for (QQuickItem* clock : clocks) {
        const auto times = panelItems(clock, QStringLiteral("worldClockTime"));
        const auto zones = panelItems(clock, QStringLiteral("worldClockZone"));
        const auto readings = panelItems(clock, QStringLiteral("worldClockReading"));
        check(times.size() == 1 && zones.size() == 1 && readings.size() == 1 &&
                  !clock->property("widgetInstanceId").toString().isEmpty(),
              "each clock widget contains exactly one identified timezone reading");
        if (times.size() != 1 || zones.size() != 1 || readings.size() != 1) continue;
        QQuickItem* time = times.front();
        QQuickItem* zone = zones.front();
        const QRectF clockBounds = panelBounds(clock, root);
        const QRectF timeBounds = panelBounds(time, root);
        const QRectF zoneBounds = panelBounds(zone, root);
        check(std::abs(clock->width() - clock->implicitWidth()) <= 1.0 &&
                  time->property("text").toString().contains(QLatin1Char(':')) &&
                  zone->isVisible() && !zone->property("text").toString().isEmpty() &&
                  zone->width() + 0.5 >= zone->implicitWidth() &&
                  time->width() + 0.5 >= time->implicitWidth() &&
                  clockBounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(timeBounds) &&
                  clockBounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(zoneBounds),
              "every clock keeps its complete label and time at natural width");
        const QFontMetricsF metrics(time->property("font").value<QFont>());
        check(std::abs(timeBounds.left() - zoneBounds.right() -
                       metrics.horizontalAdvance(QStringLiteral("  "))) <= 1.0,
              "timezone and time keep exactly two character spaces at every panel width");
    }
}

bool sameBounds(const w11widget::Rect& first, const w11widget::Rect& second) {
    return first.left == second.left && first.top == second.top &&
           first.right == second.right && first.bottom == second.bottom;
}

struct Fixture {
    w11widget::windows::TaskbarSnapshot snapshot;
    w11widget::windows::TaskbarSnapshot secondarySnapshot;
    bool probeAvailable = true;
    bool secondaryAvailable = true;
    bool fullscreen = false;
    bool showSucceeds = true;
    bool retainSucceeds = true;
    const std::string probeError = "controller-test-probe-failure";
    const QString nativeError = QStringLiteral("controller-test-native-show-failure");
    const QString retainError = QStringLiteral("controller-test-native-retain-failure");
    std::vector<w11widget::Rect> nativeCalls;
    std::vector<w11widget::Rect> retainCalls;
    QString lastMonitorProbe;
    std::function<void()> beforeShow;

    Fixture() {
        // This is a fixture identity, never used by a native API. The controller
        // still creates its actual hidden QML window through normal startup.
        snapshot.hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1));
        snapshot.bounds = {0, 1032, 1920, 1080};
        snapshot.monitorBounds = {0, 0, 1920, 1080};
        snapshot.workArea = {0, 0, 1920, 1032};
        snapshot.edge = 3;
        snapshot.dpi = 96;
        snapshot.monitorDevice = L"\\\\.\\DISPLAY1";
        secondarySnapshot = snapshot;
        secondarySnapshot.hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(2));
        secondarySnapshot.bounds = {1920, 1032, 3840, 1080};
        secondarySnapshot.monitorBounds = {1920, 0, 3840, 1080};
        secondarySnapshot.workArea = {1920, 0, 3840, 1032};
        secondarySnapshot.primary = false;
        secondarySnapshot.monitorDevice = L"\\\\.\\DISPLAY2";
    }

    w11widget::ControllerServices services(const QString& settingsPath = {}) {
        w11widget::ControllerServices result;
        // Noninteractive scenarios never use the user's settings file.
        result.settingsPath = settingsPath;
        result.readTaskbar = [this](std::string& error)
            -> std::optional<w11widget::windows::TaskbarSnapshot> {
            if (!probeAvailable) {
                error = probeError;
                return std::nullopt;
            }
            error.clear();
            return snapshot;
        };
        result.monitors = [this](std::string& error) {
            error.clear();
            return std::vector<w11widget::windows::MonitorDescription>{
                {snapshot.monitorDevice, snapshot.monitorBounds, true},
                {secondarySnapshot.monitorDevice, secondarySnapshot.monitorBounds, false}
            };
        };
        result.readTaskbarForMonitor = [this](const QString& device, std::string& error)
            -> std::optional<w11widget::windows::TaskbarSnapshot> {
            lastMonitorProbe = device;
            if (!probeAvailable) {
                error = probeError;
                return std::nullopt;
            }
            error.clear();
            if (device == QStringLiteral("primary") ||
                device == QString::fromStdWString(snapshot.monitorDevice)) {
                return snapshot;
            }
            if (device == QString::fromStdWString(secondarySnapshot.monitorDevice)) {
                if (!secondaryAvailable) {
                    error = probeError;
                    return std::nullopt;
                }
                return secondarySnapshot;
            }
            error = "controller-test-unknown-monitor";
            return std::nullopt;
        };
        result.fullscreenConflict = [this](const w11widget::windows::TaskbarSnapshot&, WId) {
            return fullscreen;
        };
        result.showPanel = [this](const w11widget::Rect& bounds, QString& error) {
            nativeCalls.push_back(bounds);
            if (beforeShow) beforeShow();
            error = showSucceeds ? QString{} : nativeError;
            return showSucceeds;
        };
        result.retainPanel = [this](const w11widget::Rect& bounds, QString& error) {
            retainCalls.push_back(bounds);
            error = retainSucceeds ? QString{} : retainError;
            return retainSucceeds;
        };
        return result;
    }
};

struct Controls {
    QDialog* dialog = nullptr;
    QComboBox* side = nullptr;
    QSpinBox* width = nullptr;
    QSpinBox* offset = nullptr;
    QComboBox* monitor = nullptr;
    QSpinBox* opacity = nullptr;
    QCheckBox* confirmation = nullptr;
    QPushButton* show = nullptr;
    QPushButton* hide = nullptr;
    QPushButton* reset = nullptr;
    QLabel* status = nullptr;
    QLabel* preview = nullptr;
    QLabel* settingsStatus = nullptr;
    QTimer* watchdog = nullptr;

    bool find() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("panelControls")) {
                dialog = qobject_cast<QDialog*>(widget);
                break;
            }
        }
        if (!dialog) return false;
        side = dialog->findChild<QComboBox*>(QStringLiteral("panelSide"));
        width = dialog->findChild<QSpinBox*>(QStringLiteral("panelWidth"));
        offset = dialog->findChild<QSpinBox*>(QStringLiteral("panelOffset"));
        monitor = dialog->findChild<QComboBox*>(QStringLiteral("panelMonitor"));
        opacity = dialog->findChild<QSpinBox*>(QStringLiteral("panelOpacity"));
        confirmation = dialog->findChild<QCheckBox*>(QStringLiteral("confirmRegion"));
        show = dialog->findChild<QPushButton*>(QStringLiteral("showPanel"));
        hide = dialog->findChild<QPushButton*>(QStringLiteral("hidePanel"));
        reset = dialog->findChild<QPushButton*>(QStringLiteral("resetPreferences"));
        status = dialog->findChild<QLabel*>(QStringLiteral("panelStatus"));
        preview = dialog->findChild<QLabel*>(QStringLiteral("placementPreview"));
        settingsStatus = dialog->findChild<QLabel*>(QStringLiteral("settingsStatus"));
        return side && width && offset && monitor && opacity &&
               confirmation && show && hide && reset && status && preview && settingsStatus;
    }
};

void invoke(w11widget::ApplicationController& controller, const char* method) {
    check(QMetaObject::invokeMethod(&controller, method, Qt::DirectConnection),
          "controller lifecycle action is invokable");
}

void powerState(w11widget::ApplicationController& controller, bool suspended) {
    check(QMetaObject::invokeMethod(&controller, "handlePowerState", Qt::DirectConnection,
                                   Q_ARG(bool, suspended)),
          "controller suspend/resume action is invokable");
}

void expectHiddenWindows() {
    for (const QWindow* window : QGuiApplication::allWindows()) {
        check(!window->isVisible(), "controller interaction tests keep all native windows hidden");
    }
}

template<typename Scenario>
void runScenario(Scenario scenario) {
    Fixture fixture;
    w11widget::ApplicationController controller(nullptr, fixture.services());
    QString error;
    if (!controller.start(error, false)) {
        std::cerr << "Controller startup failed: " << error.toStdString() << '\n';
        ++failures;
        return;
    }
    Controls controls;
    if (!controls.find()) {
        check(false, "controller exposes its calibration controls");
        return;
    }
    controls.watchdog = controller.findChild<QTimer*>(QStringLiteral("safetyWatchdog"));
    if (!controls.watchdog) {
        check(false, "controller exposes its conditional safety watchdog");
        return;
    }
    check(!controls.dialog->isVisible(), "noninteractive startup keeps controls hidden");
    check(fixture.nativeCalls.empty(), "startup never attempts to show an unconfirmed panel");
    check(!controls.watchdog->isActive(), "startup does not poll the idle desktop");
    controller.activateControls();
    check(!controls.dialog->isVisible() && fixture.nativeCalls.empty(),
          "activation requests leave noninteractive fixtures hidden");
    if constexpr (std::is_invocable_v<Scenario, Fixture&, Controls&,
                                      w11widget::ApplicationController&>) {
        scenario(fixture, controls, controller);
    } else {
        scenario(fixture, controls);
    }
    check(!controls.dialog->isVisible(), "test button clicks do not open a desktop window");
    expectHiddenWindows();
}

void expectConfirmationGuidance(const Fixture& fixture, const Controls& controls,
                                std::size_t previousCalls) {
    check(fixture.nativeCalls.size() == previousCalls,
          "unconfirmed click never reaches native panel display");
    check(controls.status->text().contains(QStringLiteral("onay"), Qt::CaseInsensitive),
          "unconfirmed click explains the required region confirmation");
}

void testButtonAndCalibration() {
    runScenario([](Fixture& fixture, Controls& controls) {
        check(controls.confirmation->isChecked(), "calibration starts confirmed for the default region");
        check(controls.show->isEnabled(), "Show remains clickable before region confirmation");
        controls.show->click();
        check(fixture.nativeCalls.size() == 1, "confirmed Show click requests one native display");
        if (!fixture.nativeCalls.empty()) {
            check(sameBounds(fixture.nativeCalls.back(), {150, 1040, 480, 1072}),
                  "default calibration matches the user's approved physical region");
        }
        check(controls.status->text().contains(QStringLiteral("göster"), Qt::CaseInsensitive),
              "successful native display gives visible Show feedback");

        controls.width->setValue(320);
        check(!controls.confirmation->isChecked(), "changing width cancels the previous approval");
        controls.show->click();
        expectConfirmationGuidance(fixture, controls, 1);
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 2, "reconfirming width permits display");

        controls.offset->setValue(32);
        check(!controls.confirmation->isChecked(), "changing offset cancels the previous approval");
        controls.show->click();
        expectConfirmationGuidance(fixture, controls, 2);
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 3, "reconfirming offset permits display");

        controls.side->setCurrentIndex(1);
        check(!controls.confirmation->isChecked(), "changing side cancels the previous approval");
        controls.show->click();
        expectConfirmationGuidance(fixture, controls, 3);
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 4, "reconfirming side permits display");
        if (fixture.nativeCalls.size() == 4) {
            check(sameBounds(fixture.nativeCalls.back(), {1568, 1040, 1888, 1072}),
                  "recalibrated right placement is sent to native display");
        }
    });
}

void testNativeFailureFeedback() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        fixture.showSucceeds = false;
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 1, "native failure is reached through actual Show click");
        check(controls.status->text().contains(fixture.nativeError),
              "native show error survives the controls refresh without generic replacement");
        check(controls.confirmation->isChecked(), "native failure preserves valid calibration for explicit retry");
        check(fixture.retainCalls.empty(), "failed initial Show has no invented rectangle to maintain");
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1 && !controls.watchdog->isActive(),
              "failed initial Show does not automatically retry without new approval");
    });
}

void testProbeFailureFeedback() {
    runScenario([](Fixture& fixture, Controls& controls) {
        controls.confirmation->setChecked(true);
        fixture.probeAvailable = false;
        controls.show->click();
        check(fixture.nativeCalls.empty(), "taskbar probe failure never reaches native display");
        check(controls.status->text().contains(QString::fromStdString(fixture.probeError)),
              "taskbar probe error survives Show recovery without generic confirmation replacement");
        check(!controls.confirmation->isChecked(), "taskbar probe failure cancels approval");
    });
}

void testChangedEnvironment() {
    runScenario([](Fixture& fixture, Controls& controls) {
        controls.confirmation->setChecked(true);
        fixture.snapshot.bounds = {0, 1024, 1920, 1080};
        controls.show->click();
        check(fixture.nativeCalls.empty(), "changed taskbar geometry blocks previously approved display");
        check(!controls.confirmation->isChecked(), "changed geometry cancels approval");
        check(controls.status->text().contains(QStringLiteral("onay"), Qt::CaseInsensitive),
              "changed geometry requests fresh confirmation");
    });
    runScenario([](Fixture& fixture, Controls& controls) {
        controls.confirmation->setChecked(true);
        fixture.snapshot.dpi = 120;
        controls.show->click();
        check(fixture.nativeCalls.empty(), "changed DPI blocks previously approved physical placement");
        check(!controls.confirmation->isChecked(), "changed DPI cancels approval");
        check(controls.status->text().contains(QStringLiteral("onay"), Qt::CaseInsensitive),
              "changed DPI requests fresh confirmation");
    });
}

void testPinnedFullscreenShow() {
    runScenario([](Fixture& fixture, Controls& controls) {
        fixture.fullscreen = true;
        fixture.snapshot.autoHide = true;
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 1, "confirmed initial Show ignores fullscreen and auto-hide gates");
        check(controls.confirmation->isChecked(), "fullscreen and auto-hide never revoke Show approval");
        check(controls.watchdog->isActive(), "pinned Show keeps its visibility maintenance active");
        check(!controls.dialog->findChild<QCheckBox*>(QStringLiteral("hideForFullscreen")),
              "controls expose no automatic fullscreen-hide preference");
    });
}

void testPinnedMaintenanceAndManualHide() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 1, "approved display starts pinned visibility intent");
        invoke(controller, "reevaluateVisibility");
        check(!fixture.retainCalls.empty(), "visible maintenance reasserts the observed placement");
        const auto retained = fixture.retainCalls.size();
        fixture.fullscreen = true;
        fixture.snapshot.autoHide = true;
        invoke(controller, "handleEnvironmentChange");
        invoke(controller, "reevaluateVisibility");
        check(fixture.retainCalls.size() > retained, "environment changes maintain rather than hide the panel");
        check(fixture.nativeCalls.size() == 1, "maintenance does not issue another initial Show");
        check(controls.confirmation->isChecked(), "automatic environment events retain placement approval");
        check(controls.watchdog->isActive(), "fullscreen and auto-hide keep pinned maintenance active");

        controls.hide->click();
        const auto beforeHide = fixture.retainCalls.size();
        check(!controls.watchdog->isActive(), "manual Hide stops pinned maintenance");
        fixture.fullscreen = false;
        fixture.snapshot.autoHide = false;
        invoke(controller, "handleEnvironmentChange");
        invoke(controller, "handleShellRecreation");
        powerState(controller, true);
        powerState(controller, false);
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1 && fixture.retainCalls.size() == beforeHide,
              "environment shell and power events cannot undo manual Hide");
    });
}

void testPinnedProbeLoss() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        if (fixture.nativeCalls.empty()) return;
        const auto observed = fixture.nativeCalls.back();
        fixture.probeAvailable = false;
        invoke(controller, "handleEnvironmentChange");
        invoke(controller, "reevaluateVisibility");
        check(!fixture.retainCalls.empty() && sameBounds(fixture.retainCalls.back(), observed),
              "missing taskbar retains the last successful observed rectangle");
        check(controls.confirmation->isChecked() && controls.watchdog->isActive(),
              "probe loss never cancels approved pinned visibility");
        check(controls.preview->text().contains(QString::fromStdString(fixture.probeError)) ||
              controls.status->text().contains(QString::fromStdString(fixture.probeError)),
              "probe loss still reports the real diagnostic failure");
        fixture.probeAvailable = true;
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1, "taskbar recovery keeps the existing panel without a new Show");
    });
}

void testPinnedGeometryChanges() {
    const auto runChange = [](auto change) {
        runScenario([change](Fixture& fixture, Controls& controls,
                             w11widget::ApplicationController& controller) {
            controls.confirmation->setChecked(true);
            controls.show->click();
            change(fixture.snapshot);
            invoke(controller, "handleEnvironmentChange");
            invoke(controller, "reevaluateVisibility");
            check(!fixture.retainCalls.empty(), "changed geometry maintains the pinned panel");
            check(controls.confirmation->isChecked(), "automatic geometry or HWND change never revokes pinning");
            check(controls.watchdog->isActive(), "automatic geometry change does not stop maintenance");
            check(fixture.nativeCalls.size() == 1, "automatic geometry change does not require another Show");
        });
    };
    runChange([](w11widget::windows::TaskbarSnapshot& snapshot) { snapshot.bounds.top -= 8; });
    runChange([](w11widget::windows::TaskbarSnapshot& snapshot) { snapshot.dpi = 120; });
    runChange([](w11widget::windows::TaskbarSnapshot& snapshot) {
        snapshot.hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(3));
    });
}

void testMissingSelectedMonitorNeverFallsBackToPrimary() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        const QString device = QString::fromStdWString(fixture.secondarySnapshot.monitorDevice);
        const int secondaryIndex = controls.monitor->findData(device);
        if (secondaryIndex < 0) {
            check(false, "selected monitor fallback scenario has an injected secondary display");
            return;
        }
        controls.monitor->setCurrentIndex(secondaryIndex);
        controls.confirmation->setChecked(true);
        controls.show->click();
        if (fixture.nativeCalls.empty()) return;
        const auto secondaryBounds = fixture.nativeCalls.back();
        fixture.secondaryAvailable = false;
        invoke(controller, "handleEnvironmentChange");
        invoke(controller, "reevaluateVisibility");
        check(fixture.lastMonitorProbe == device && !fixture.retainCalls.empty() &&
              sameBounds(fixture.retainCalls.back(), secondaryBounds),
              "unavailable selected taskbar keeps its observed coordinates instead of moving to primary");
        check(controls.watchdog->isActive() && fixture.nativeCalls.size() == 1,
              "unavailable selected monitor remains pinned without a fresh Show");
    });
}

void testPinnedShellRecreation() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        fixture.probeAvailable = false;
        invoke(controller, "handleShellRecreation");
        invoke(controller, "reevaluateVisibility");
        check(controls.confirmation->isChecked() && controls.watchdog->isActive(),
              "shell recreation keeps cached approval and pinned intent");
        check(!fixture.retainCalls.empty(), "shell recreation maintains the existing overlay while discovery recovers");
        fixture.probeAvailable = true;
        fixture.snapshot.hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(3));
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1, "recreated taskbar does not require explicit Show again");
    });
}

void testPinnedPowerEvents() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        powerState(controller, true);
        invoke(controller, "reevaluateVisibility");
        check(controls.confirmation->isChecked() && controls.watchdog->isActive(),
              "power event does not cancel pinned maintenance or approval");
        fixture.snapshot.dpi = 120;
        powerState(controller, false);
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1 && !fixture.retainCalls.empty(),
              "resume and DPI changes maintain the existing pinned panel");
        controls.hide->click();
        const auto retained = fixture.retainCalls.size();
        powerState(controller, true);
        powerState(controller, false);
        invoke(controller, "reevaluateVisibility");
        check(fixture.retainCalls.size() == retained && !controls.watchdog->isActive(),
              "power recovery cannot undo manual Hide");
    });
}

void testPendingCalibrationKeepsPinnedRectangle() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        if (fixture.nativeCalls.empty()) return;
        const auto original = fixture.nativeCalls.back();
        controls.width->setValue(320);
        check(!controls.confirmation->isChecked(), "new width requires fresh placement approval");
        invoke(controller, "reevaluateVisibility");
        check(!fixture.retainCalls.empty() && sameBounds(fixture.retainCalls.back(), original),
              "pending width edit retains the old successful rectangle");
        check(controls.watchdog->isActive(), "pending calibration never self-hides the current panel");
        controls.show->click();
        expectConfirmationGuidance(fixture, controls, 1);
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 2 && sameBounds(fixture.nativeCalls.back(), {150, 1040, 470, 1072}),
              "newly approved width is applied by explicit Show");

        const auto beforeMonitorEdit = fixture.nativeCalls.back();
        const QString device = QString::fromStdWString(fixture.secondarySnapshot.monitorDevice);
        const int secondaryIndex = controls.monitor->findData(device);
        if (secondaryIndex < 0) {
            check(false, "pending monitor calibration has an injected secondary display");
            return;
        }
        controls.monitor->setCurrentIndex(secondaryIndex);
        invoke(controller, "reevaluateVisibility");
        check(!fixture.retainCalls.empty() && sameBounds(fixture.retainCalls.back(), beforeMonitorEdit),
              "pending monitor edit cannot move the pinned panel to another monitor");
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 3 && sameBounds(fixture.nativeCalls.back(), {2070, 1040, 2390, 1072}),
              "explicitly approved monitor edit applies the observed secondary placement");
    });
}

void testExistingShowMaintainsCachedApproval() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        if (fixture.nativeCalls.empty()) return;
        const auto observed = fixture.nativeCalls.back();
        controls.confirmation->setChecked(false);
        fixture.probeAvailable = false;
        controls.show->click();
        check(fixture.nativeCalls.size() == 1 && !fixture.retainCalls.empty() &&
              sameBounds(fixture.retainCalls.back(), observed),
              "Show of unchanged preferences maintains cached approval despite unchecked candidate and probe loss");
        check(controls.watchdog->isActive(), "Show of already pinned preferences never cancels visibility");
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1, "cached Show never repeats the initial placement call");
    });
}

void testPreferencesPersistenceAndReset() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        check(false, "settings test owns a writable temporary directory");
        return;
    }
    const QString settingsPath = temporary.filePath(QStringLiteral("panel.json"));
    Fixture fixture;
    const QString secondaryDevice = QString::fromStdWString(fixture.secondarySnapshot.monitorDevice);
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(settingsPath));
        QString error;
        if (!controller.start(error, false)) {
            check(false, "controller starts with an explicit temporary settings file");
            return;
        }
        Controls controls;
        if (!controls.find()) {
            check(false, "persistence scenario exposes preferences controls");
            return;
        }
        const int secondaryIndex = controls.monitor->findData(secondaryDevice);
        if (secondaryIndex < 0) {
            check(false, "monitor selector uses the injected secondary display identity");
            return;
        }
        controls.monitor->setCurrentIndex(secondaryIndex);
        controls.side->setCurrentIndex(1);
        controls.width->setValue(420);
        controls.offset->setValue(44);
        controls.opacity->setValue(73);
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 1, "secondary monitor calibration reaches only injected display");
        check(fixture.lastMonitorProbe == secondaryDevice, "selected monitor uses the injected monitor probe");
        invoke(controller, "savePreferences");
        const auto saved = w11widget::SettingsStore(settingsPath).load();
        check(saved.ok && !saved.missing, "controller saves a valid settings document");
        check(saved.settings.monitor == secondaryDevice &&
              saved.settings.side == w11widget::PanelSide::Right &&
              saved.settings.widthDip == 420 && saved.settings.offsetDip == 44 &&
              saved.settings.opacity == 0.73 && !saved.settings.hideForFullscreen,
              "controller persists every preference from its actual controls");
        expectHiddenWindows();
    }
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(settingsPath));
        QString error;
        if (!controller.start(error, false)) {
            check(false, "controller restarts from saved temporary preferences");
            return;
        }
        Controls controls;
        if (!controls.find()) {
            check(false, "recreated controller exposes loaded preferences");
            return;
        }
        controls.watchdog = controller.findChild<QTimer*>(QStringLiteral("safetyWatchdog"));
        if (!controls.watchdog) {
            check(false, "recreated controller exposes the conditional watchdog");
            return;
        }
        check(controls.monitor->currentData().toString() == secondaryDevice &&
              controls.side->currentIndex() == 1 && controls.width->value() == 420 &&
              controls.offset->value() == 44 && controls.opacity->value() == 73,
              "startup restores saved monitor side size offset and opacity");
        check(controls.confirmation->isChecked(), "startup selects the default-region confirmation");
        check(!controls.watchdog->isActive(), "startup never resumes persisted visibility intent");
        check(fixture.nativeCalls.size() == 1, "loading preferences never automatically displays the panel");

        controls.confirmation->setChecked(true);
        controls.show->click();
        const auto beforeReset = fixture.nativeCalls.size();
        controls.reset->click();
        check(controls.monitor->currentData().toString() == QStringLiteral("primary") &&
              controls.side->currentIndex() == 0 && controls.width->value() == 330 &&
              controls.offset->value() == 150 && controls.opacity->value() == 95,
              "Reset restores safe default preferences");
        check(!controls.confirmation->isChecked() && !controls.watchdog->isActive(),
              "Reset clears approval and visible intent");
        const auto reset = w11widget::SettingsStore(settingsPath).load();
        check(reset.ok && !reset.missing && reset.settings == w11widget::PanelSettings{},
              "Reset writes a valid default settings file");
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == beforeReset, "Reset cannot be undone by a later safety evaluation");
        expectHiddenWindows();
    }
}

bool writeFixtureFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    check(opened, "temporary fixture file is writable");
    if (!opened) return false;
    const bool written = file.write(bytes) == bytes.size();
    check(written, "temporary fixture bytes are written completely");
    return written;
}

QByteArray fixtureBytes(const QString& path) {
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    check(opened, "temporary fixture file remains readable");
    return opened ? file.readAll() : QByteArray{};
}

void testLegacyFullscreenPreferenceCannotHidePinnedPanel() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        check(false, "legacy settings scenario owns a temporary directory");
        return;
    }
    const QString path = temporary.filePath(QStringLiteral("panel.json"));
    const QByteArray legacy(
        "{\"schema_version\":1,\"panel\":{\"monitor\":\"primary\",\"side\":\"left\","
        "\"width_dip\":330,\"offset_dip\":150,\"opacity\":0.95,\"hide_for_fullscreen\":true}}");
    if (!writeFixtureFile(path, legacy)) return;
    Fixture fixture;
    fixture.fullscreen = true;
    w11widget::ApplicationController controller(nullptr, fixture.services(path));
    QString error;
    if (!controller.start(error, false)) {
        check(false, "legacy fullscreen settings remain loadable");
        return;
    }
    Controls controls;
    if (!controls.find()) {
        check(false, "legacy settings expose current controls");
        return;
    }
    controls.confirmation->setChecked(true);
    controls.show->click();
    invoke(controller, "reevaluateVisibility");
    check(fixture.nativeCalls.size() == 1 && !fixture.retainCalls.empty(),
          "saved automatic-fullscreen-hide preference cannot override explicit pinned Show");
    check(!controls.dialog->findChild<QCheckBox*>(QStringLiteral("hideForFullscreen")),
          "legacy fullscreen preference has no automatic-hide control");
    controls.opacity->setValue(90);
    invoke(controller, "savePreferences");
    const auto saved = w11widget::SettingsStore(path).load();
    check(saved.ok && !saved.settings.hideForFullscreen,
          "saving current preferences normalizes the former fullscreen-hide flag");
    expectHiddenWindows();
}

void testMalformedSettingsPreservation() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        check(false, "malformed settings scenario owns a temporary directory");
        return;
    }
    const QString path = temporary.filePath(QStringLiteral("panel.json"));
    const QByteArray malformed("{\"schema_version\":1,\"panel\":");
    if (!writeFixtureFile(path, malformed)) return;
    Fixture fixture;
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(path));
        QString error;
        if (!controller.start(error, false)) {
            check(false, "malformed settings fall back without preventing startup");
            return;
        }
        Controls controls;
        if (!controls.find()) {
            check(false, "malformed settings scenario exposes controls");
            return;
        }
        check(fixtureBytes(path) == malformed, "startup preserves malformed settings bytes");
        controls.confirmation->setChecked(true);
        controls.show->click();
        controls.hide->click();
        check(fixtureBytes(path) == malformed, "Show and Hide do not replace malformed settings");
        expectHiddenWindows();
    }
    check(fixtureBytes(path) == malformed, "shutdown without a preference edit preserves malformed settings");
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(path));
        QString error;
        if (!controller.start(error, false)) {
            check(false, "malformed settings remain recoverable through explicit Reset");
            return;
        }
        Controls controls;
        if (!controls.find()) {
            check(false, "Reset recovery exposes controls");
            return;
        }
        controls.reset->click();
        const auto reset = w11widget::SettingsStore(path).load();
        check(reset.ok && !reset.missing && reset.settings == w11widget::PanelSettings{},
              "explicit Reset replaces malformed settings with valid defaults");
        expectHiddenWindows();
    }
}

void testPendingPreferencesFlushAtShutdown() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        check(false, "pending save scenario owns a temporary directory");
        return;
    }
    const QString path = temporary.filePath(QStringLiteral("panel.json"));
    Fixture fixture;
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(path));
        QString error;
        if (!controller.start(error, false)) {
            check(false, "pending save scenario starts");
            return;
        }
        Controls controls;
        if (!controls.find()) {
            check(false, "pending save scenario exposes preferences controls");
            return;
        }
        controls.width->setValue(512);
        controls.offset->setValue(88);
        check(!QFile::exists(path), "preference edits remain pending before the debounce runs");
        // No event processing, delay or explicit save: destroy before debounce.
        expectHiddenWindows();
    }
    const auto saved = w11widget::SettingsStore(path).load();
    check(saved.ok && !saved.missing && saved.settings.widthDip == 512 &&
          saved.settings.offsetDip == 88,
          "shutdown flushes preference edits without waiting for the save debounce");
}

void testFailedSaveRemainsDirty() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        check(false, "failed save retry owns a temporary directory");
        return;
    }
    const QString parentPath = temporary.filePath(QStringLiteral("blocked-parent"));
    if (!writeFixtureFile(parentPath, QByteArray("temporary directory blocker"))) return;
    const QString path = QDir(parentPath).filePath(QStringLiteral("panel.json"));
    Fixture fixture;
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(path));
        QString error;
        if (!controller.start(error, false)) {
            check(false, "failed save scenario starts with defaults");
            return;
        }
        Controls controls;
        if (!controls.find()) {
            check(false, "failed save scenario exposes preferences controls");
            return;
        }
        controls.width->setValue(500);
        controls.offset->setValue(92);
        invoke(controller, "savePreferences");
        check(!QFile::exists(path), "blocked parent prevents the first settings save");
        check(controls.settingsStatus->text().contains(QStringLiteral("kaydedilemedi")),
              "failed settings save reports its error");
        // Both paths belong to this test's QTemporaryDir; no user file is removed.
        if (!QFile::remove(parentPath)) {
            check(false, "temporary blocker file can be removed");
            return;
        }
        if (!QDir().mkpath(parentPath)) {
            check(false, "temporary parent can be repaired as a directory");
            return;
        }
        check(!QFile::exists(path), "repair does not save through a second edit or manual retry");
        expectHiddenWindows();
    }
    const auto saved = w11widget::SettingsStore(path).load();
    check(saved.ok && !saved.missing && saved.settings.widthDip == 500 &&
          saved.settings.offsetDip == 92,
          "failed save stays dirty and retries successfully during shutdown");
}

void testNativeMaintenanceFailureKeepsPinned() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        if (fixture.nativeCalls.empty()) return;
        const auto observed = fixture.nativeCalls.back();
        fixture.snapshot.dpi = 120;
        fixture.retainSucceeds = false;
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1, "pinned maintenance failures never issue another initial Show");
        check(controls.confirmation->isChecked() && controls.watchdog->isActive(),
              "native maintenance failure preserves successful Show intent and approval");
        check(controls.status->text().contains(fixture.retainError),
              "native maintenance failure retains its actual diagnostic error");
        fixture.retainSucceeds = true;
        fixture.probeAvailable = false;
        const auto retained = fixture.retainCalls.size();
        invoke(controller, "reevaluateVisibility");
        check(fixture.retainCalls.size() > retained && fixture.nativeCalls.size() == 1 &&
              sameBounds(fixture.retainCalls.back(), observed),
              "failed maintenance never replaces last successful coordinates used when probes disappear");
    });
}

void testFailedRepositionKeepsPinned() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        if (fixture.nativeCalls.empty()) return;
        const auto original = fixture.nativeCalls.back();
        controls.width->setValue(320);
        controls.confirmation->setChecked(true);
        fixture.showSucceeds = false;
        controls.show->click();
        check(fixture.nativeCalls.size() == 2 && controls.watchdog->isActive(),
              "failed explicit reposition preserves the already pinned panel");
        check(controls.status->text().contains(fixture.nativeError),
              "failed reposition reports the native error");
        invoke(controller, "reevaluateVisibility");
        check(!fixture.retainCalls.empty() && sameBounds(fixture.retainCalls.back(), original),
              "failed reposition maintains the previous successful rectangle");
        fixture.showSucceeds = true;
        controls.show->click();
        check(fixture.nativeCalls.size() == 3 &&
              sameBounds(fixture.nativeCalls.back(), {150, 1040, 470, 1072}),
              "explicit retry applies the approved replacement rectangle");
    });
}

void testOwnWindowEventsDuringShow() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        fixture.beforeShow = [&] {
            invoke(controller, "handleEnvironmentChange");
            powerState(controller, true);
        };
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 1 && controls.confirmation->isChecked() &&
              controls.watchdog->isActive(),
              "own-window environment events cannot cancel a successful Show");
        invoke(controller, "reevaluateVisibility");
        check(fixture.nativeCalls.size() == 1 && !fixture.retainCalls.empty(),
              "own-window event recovery maintains the successful Show without another initial attempt");
    });
}

void testPinnedInvalidPlacementKeepsObservedRectangle() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController& controller) {
        controls.confirmation->setChecked(true);
        controls.show->click();
        if (fixture.nativeCalls.empty()) return;
        const auto observed = fixture.nativeCalls.back();
        fixture.snapshot.dpi = 960;
        invoke(controller, "handleEnvironmentChange");
        invoke(controller, "reevaluateVisibility");
        check(!fixture.retainCalls.empty() && sameBounds(fixture.retainCalls.back(), observed),
              "unusable new placement keeps the successful observed rectangle");
        check(controls.confirmation->isChecked() && controls.watchdog->isActive(),
              "unusable new placement cannot cancel pinned visibility");
    });
}

w11widget::WidgetHostModel* findWidgetHost() {
    for (QWindow* window : QGuiApplication::allWindows()) {
        if (auto* panel = qobject_cast<w11widget::PanelWindow*>(window)) return panel->widgetHost();
    }
    check(false, "controller owns an accessible widget host");
    return nullptr;
}

void testWidgetLifecycleAndPlacementIndependence() {
    runScenario([](Fixture& fixture, Controls& controls,
                   w11widget::ApplicationController&) {
        auto* host = findWidgetHost();
        if (!host) return;
        check(host->rowCount() == 4 && !host->isRunning(),
              "startup prepares three independent clocks and Countdown without activating them");
        QString error;
        check(host->addWidget(QStringLiteral("sample"), error),
              "controller lifecycle fixture can add an independent sample module");
        auto* manage = controls.dialog->findChild<QPushButton*>(QStringLiteral("manageWidgets"));
        check(manage != nullptr, "panel controls expose widget management");
        if (manage) manage->click();
        auto* pages = controls.dialog->findChild<QStackedWidget*>(QStringLiteral("controlPages"));
        auto* manager = controls.dialog->findChild<QDialog*>(QStringLiteral("widgetSettingsDialog"));
        auto* back = controls.dialog->findChild<QPushButton*>(QStringLiteral("widgetSettingsBack"));
        check(pages && manager && back && pages->currentWidget() == manager,
              "widget management opens as a control-window page");
        if (back) back->click();
        check(pages && pages->currentWidget()->objectName() == QStringLiteral("panelControlsPage"),
              "widget manager back action returns to panel controls");
        expectHiddenWindows();
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(host->isRunning(), "successful requested Show activates local modules");
        const auto& configuration = host->configuration();
        const auto sample = std::find_if(configuration.begin(), configuration.end(),
            [](const auto& item) { return item.typeId == QStringLiteral("sample"); });
        if (sample == configuration.end()) {
            check(false, "controller lifecycle fixture contains the added sample");
            return;
        }
        const int sampleRow = static_cast<int>(std::distance(configuration.begin(), sample));
        const QString id = sample->instanceId;
        QObject* model = host->data(host->index(sampleRow, 0),
                                    w11widget::WidgetHostModel::ViewModelRole).value<QObject*>();
        check(model && model->property("active").toBool(), "shown sample has an active view model");
        if (!model) return;
        check(QMetaObject::invokeMethod(model, "trigger", Qt::DirectConnection), "sample interaction is invokable");
        check(model->property("counter").toInt() == 1, "active sample interaction updates its own counter");
        const auto showCount = fixture.nativeCalls.size();
        const auto retainCount = fixture.retainCalls.size();
        check(host->setWidth(id, 145, error) && host->moveWidget(id, 0, error),
              "widget width and order can change while the panel is requested");
        QJsonObject settings = host->configuration()[0].settings;
        settings.insert(QStringLiteral("label"), QStringLiteral("Yeni etiket"));
        check(host->setWidgetSettings(id, settings, error), "per-widget settings apply to the selected module");
        check(host->data(host->index(0, 0), w11widget::WidgetHostModel::ViewModelRole).value<QObject*>() == model &&
              model->property("counter").toInt() == 1,
              "editing and reordering preserve the module's live state");
        check(fixture.nativeCalls.size() == showCount && fixture.retainCalls.size() == retainCount &&
              controls.confirmation->isChecked() && controls.watchdog->isActive(),
              "widget edits neither reposition the native panel nor revoke placement approval");
        controls.hide->click();
        check(!host->isRunning() && !model->property("active").toBool(),
              "explicit Hide suspends modules with the panel");
        QMetaObject::invokeMethod(model, "trigger", Qt::DirectConnection);
        check(model->property("counter").toInt() == 1, "hidden module does not process sample actions");
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(host->isRunning() && model->property("active").toBool() && model->property("counter").toInt() == 1,
              "Show resumes the same module without losing state");
        expectHiddenWindows();
    });
}

void testWidgetPreferencesSurvivePanelEditsAndRestart() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) { check(false, "widget persistence owns a temporary directory"); return; }
    const QString path = temporary.filePath(QStringLiteral("widgets.json"));
    w11widget::PanelSettings original;
    original.widthDip = 450;
    original.widgets[1].visible = false;
    original.widgets[1].settings.insert(QStringLiteral("show_date"), true);
    original.widgets[1].settings.insert(QStringLiteral("label"), QStringLiteral("Ofis saati"));
    original.widgets.push_back({QStringLiteral("future-1"), QStringLiteral("future.widget"), true, true, 80,
                               {{QStringLiteral("option"), 7}}});
    QString error;
    check(w11widget::SettingsStore(path).save(original, error), "custom widget fixture is saved atomically");
    const QByteArray before = fixtureBytes(path);
    Fixture fixture;
    w11widget::WidgetConfigurations expected;
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(path));
        if (!controller.start(error, false)) { check(false, "controller loads widget preferences"); return; }
        Controls controls;
        if (!controls.find()) { check(false, "widget persistence exposes panel controls"); return; }
        auto* host = findWidgetHost();
        if (!host) return;
        check(host->configuration() == original.widgets && fixtureBytes(path) == before,
              "startup restores configured widgets and preserves the existing file bytes");
        QObject* clock = widgetModel(host, QStringLiteral("world-clock-1"));
        check(clock && clock->property("displayName").toString() == QStringLiteral("Ofis saati"),
              "startup restores a clock's saved custom panel name");
        QObject* newYorkClock = widgetModel(host, QStringLiteral("world-clock-2"));
        QObject* istanbulClock = widgetModel(host, QStringLiteral("world-clock-3"));
        check(newYorkClock && istanbulClock &&
                  newYorkClock->property("displayName").toString() == QStringLiteral("New York") &&
                  istanbulClock->property("displayName").toString() == QStringLiteral("İstanbul") &&
                  !host->configuration()[widgetRow(host, QStringLiteral("world-clock-2"))].settings.contains(QStringLiteral("label")) &&
                  !host->configuration()[widgetRow(host, QStringLiteral("world-clock-3"))].settings.contains(QStringLiteral("label")) &&
                  fixtureBytes(path) == before,
              "omitted clock labels resolve to default city names without changing startup preferences");
        controls.opacity->setValue(76);
        invoke(controller, "savePreferences");
        auto saved = w11widget::SettingsStore(path).load();
        check(saved.ok && saved.settings.widgets == original.widgets && saved.settings.widthDip == 450,
              "saving unrelated opacity preserves configured and unavailable widgets");
        check(host->moveWidget(QStringLiteral("world-clock-1"), 2, error) &&
              host->setEnabled(QStringLiteral("future-1"), false, error),
              "widget preferences emit edits to the controller save path");
        auto clockSettings = host->configuration()[widgetRow(host, QStringLiteral("world-clock-1"))].settings;
        clockSettings.insert(QStringLiteral("label"), QStringLiteral("Tokyo ofisi"));
        clockSettings.insert(QStringLiteral("time_zone"), QStringLiteral("Asia/Tokyo"));
        check(host->setWidgetSettings(QStringLiteral("world-clock-1"), clockSettings, error) &&
                  clock && QMetaObject::invokeMethod(clock, "refreshNow", Qt::DirectConnection) &&
                  clock->property("displayName").toString() == QStringLiteral("Tokyo ofisi"),
              "editing a clock's name and timezone preserves its name through minute refresh");
        expected = host->configuration();
        // Normal shutdown must flush the pending widget edits before debounce.
    }
    auto saved = w11widget::SettingsStore(path).load();
    check(saved.ok && saved.settings.widgets == expected && saved.settings.opacity == 0.76,
          "shutdown flushes pending module order and flags without losing panel preferences");
    {
        w11widget::ApplicationController controller(nullptr, fixture.services(path));
        if (!controller.start(error, false)) { check(false, "widget preferences reopen"); return; }
        auto* host = findWidgetHost();
        check(host && host->configuration() == expected && !host->isRunning(),
              "reopening restores modules and keeps startup hidden/inactive");
        QObject* clock = host ? widgetModel(host, QStringLiteral("world-clock-1")) : nullptr;
        const auto readings = clock ? clock->property("clocks").toList() : QVariantList{};
        check(clock && clock->property("displayName").toString() == QStringLiteral("Tokyo ofisi") &&
                  readings.size() == 1 &&
                  readings.front().toMap().value(QStringLiteral("zoneId")).toString() == QStringLiteral("Asia/Tokyo"),
              "saved custom clock names and their independent timezones survive reopening");
        Controls controls;
        if (!controls.find()) { check(false, "widget reset exposes controls"); return; }
        controls.reset->click();
        saved = w11widget::SettingsStore(path).load();
        check(saved.ok && saved.settings == w11widget::PanelSettings{} && host &&
              host->configuration() == w11widget::defaultWidgetConfigurations(),
              "explicit Reset restores both panel and default module preferences");
        expectHiddenWindows();
    }
}

void testIndependentClockLifecycleAndPlacement() {
    runScenario([](Fixture& fixture, Controls& controls, w11widget::ApplicationController&) {
        auto* host = findWidgetHost();
        if (!host) return;
        const QString firstId = QStringLiteral("world-clock-1");
        const QString secondId = QStringLiteral("world-clock-2");
        const QString thirdId = QStringLiteral("world-clock-3");
        QPointer<QObject> first = widgetModel(host, firstId);
        QPointer<QObject> second = widgetModel(host, secondId);
        QPointer<QObject> third = widgetModel(host, thirdId);
        check(first && second && third && first != second && second != third && first != third,
              "default clocks own three distinct view models before activation");
        if (!first || !second || !third) return;
        controls.show->click();
        check(host->isRunning() && first->property("active").toBool() &&
                  second->property("active").toBool() && third->property("active").toBool(),
              "Show activates each independent clock");
        const auto showCount = fixture.nativeCalls.size();
        const auto retainCount = fixture.retainCalls.size();
        const QJsonObject secondSettings = host->configuration()[widgetRow(host, secondId)].settings;
        const QJsonObject thirdSettings = host->configuration()[widgetRow(host, thirdId)].settings;
        QString error;
        check(host->setWidgetSettings(firstId,
                  {{QStringLiteral("show_date"), true},
                   {QStringLiteral("time_zone"), QStringLiteral("Europe/London")}}, error),
              "one clock's timezone and date setting can be edited while running");
        check(first->property("showDate").toBool() &&
                  host->configuration()[widgetRow(host, secondId)].settings == secondSettings &&
                  host->configuration()[widgetRow(host, thirdId)].settings == thirdSettings,
              "editing one clock preserves other timezone and date settings");
        const auto checkOtherClocks = [&] {
            check(widgetModel(host, secondId) == second && widgetModel(host, thirdId) == third &&
                      second->property("active").toBool() && third->property("active").toBool(),
                  "other clock instances and active lifecycle survive an individual clock edit");
            check(fixture.nativeCalls.size() == showCount && fixture.retainCalls.size() == retainCount &&
                      controls.confirmation->isChecked() && controls.watchdog->isActive(),
                  "individual clock edits preserve approved native placement and panel maintenance");
        };
        checkOtherClocks();
        check(host->setVisible(firstId, false, error) && !first->property("active").toBool(),
              "hiding one clock suspends only that clock");
        checkOtherClocks();
        check(host->setVisible(firstId, true, error) && host->moveWidget(firstId, host->rowCount() - 1, error) &&
                  widgetModel(host, firstId) == first && first->property("active").toBool(),
              "restoring and reordering one clock preserves its live model");
        checkOtherClocks();
        check(host->setEnabled(firstId, false, error) && first.isNull(),
              "disabling one clock shuts down only its model");
        checkOtherClocks();
        check(host->setEnabled(firstId, true, error), "an individual clock can be enabled again");
        QPointer<QObject> restored = widgetModel(host, firstId);
        check(restored && restored->property("active").toBool() && restored->property("showDate").toBool(),
              "re-enabled clock restores its own saved settings and active state");
        check(host->removeWidget(firstId, error) && restored.isNull(),
              "removing one clock shuts down only its module");
        checkOtherClocks();
        controls.hide->click();
        check(!host->isRunning() && !second->property("active").toBool() && !third->property("active").toBool(),
              "explicit Hide suspends the remaining independent clocks");
    });
}

void testIndependentClockPanelLayout() {
    // Load the packaged QML and real widget models while the native panel stays
    // hidden. Content width may exceed the viewport without changing placement.
    w11widget::PanelWindow panel;
    QString error;
    if (!panel.initialize(error)) {
        std::cerr << "Hidden layout startup failed: " << error.toStdString() << '\n';
        check(false, "independent clock layout loads the real hidden QML panel");
        return;
    }
    auto* root = panel.rootObject();
    auto* host = panel.widgetHost();
    auto configuration = w11widget::defaultWidgetConfigurations();
    configuration.push_back({QStringLiteral("world-clock-layout-fourth"), QStringLiteral("world-clock"),
                             true, true, 320,
                             {{QStringLiteral("show_date"), false},
                              {QStringLiteral("time_zone"), QStringLiteral("Pacific/Chatham")}}});
    const QString countdownId = QStringLiteral("countdown-1");
    const QString firstClockId = QStringLiteral("world-clock-1");
    const QString secondClockId = QStringLiteral("world-clock-2");
    if (!host->setConfiguration(configuration, error)) {
        check(false, "independent layout accepts four individual clock instances");
        return;
    }
    settlePanelLayout(root);
    for (const auto& [instanceId, expectedName] : std::vector<std::pair<QString, QString>>{
             {QStringLiteral("world-clock-1"), QStringLiteral("UTC")},
             {QStringLiteral("world-clock-2"), QStringLiteral("New York")},
             {QStringLiteral("world-clock-3"), QStringLiteral("İstanbul")}}) {
        auto* clock = clockItem(root, instanceId);
        auto* label = clock ? panelItem(clock, QStringLiteral("worldClockZone")) : nullptr;
        check(label && label->property("text").toString() == expectedName &&
                  !host->configuration()[widgetRow(host, instanceId)].settings.contains(QStringLiteral("label")),
              "each unnamed default clock displays its editable default name without persisting a label");
    }

    for (const int savedWidth : {144, 320}) {
        check(host->setWidth(countdownId, savedWidth, error) &&
                  host->setWidth(firstClockId, savedWidth, error),
              "layout accepts older and expanded saved widget widths");
        qreal clockWidth = 0;
        for (const int panelWidth : {330, 640}) {
            panel.resize(panelWidth, 32);
            root->setWidth(panelWidth);
            checkIndependentPanelLayout(root, 1, 4);
            auto* countdown = panelItem(root, QStringLiteral("countdownWidget"));
            auto* clock = clockItem(root, firstClockId);
            check(countdown && countdown->width() < savedWidth && clock && clock->width() < savedWidth,
                  "saved widget widths do not stretch content-sized Countdown or clock cards");
            if (clock) {
                if (clockWidth > 0)
                    check(std::abs(clockWidth - clock->width()) <= 0.5,
                          "expanding the panel leaves the clock card width and internal gap unchanged");
                clockWidth = clock->width();
            }
            auto* viewport = panelItem(root, QStringLiteral("panelWidgetViewport"));
            if (panelWidth == 640 && viewport)
                check(viewport->property("contentWidth").toReal() + 20 < viewport->width(),
                      "a wider bar leaves unused space after natural-width widgets");
        }
    }

    const QString customName = QStringLiteral("Uzun Ofis Saati Etiketi");
    QObject* namedModel = widgetModel(host, firstClockId);
    auto* secondClock = clockItem(root, secondClockId);
    auto* thirdClock = clockItem(root, QStringLiteral("world-clock-3"));
    auto* secondLabel = secondClock ? panelItem(secondClock, QStringLiteral("worldClockZone")) : nullptr;
    auto* thirdLabel = thirdClock ? panelItem(thirdClock, QStringLiteral("worldClockZone")) : nullptr;
    if (!namedModel || !secondLabel || !thirdLabel) {
        check(false, "custom-name regression exposes the independent clock models and labels");
        return;
    }
    const QString secondName = secondLabel->property("text").toString();
    const QString thirdName = thirdLabel->property("text").toString();
    auto namedSettings = host->configuration()[widgetRow(host, firstClockId)].settings;
    namedSettings.insert(QStringLiteral("label"), customName);
    check(host->updateWidgetSettings(firstClockId, namedSettings.toVariantMap()).isEmpty(),
          "editing a clock's label updates its existing widget without reopening");
    qreal namedWidth = 0;
    for (const int panelWidth : {330, 640}) {
        panel.resize(panelWidth, 32);
        root->setWidth(panelWidth);
        checkIndependentPanelLayout(root, 1, 4);
        auto* clock = clockItem(root, firstClockId);
        auto* label = clock ? panelItem(clock, QStringLiteral("worldClockZone")) : nullptr;
        auto* time = clock ? panelItem(clock, QStringLiteral("worldClockTime")) : nullptr;
        const QVariantList readings = namedModel->property("clocks").toList();
        check(widgetModel(host, firstClockId) == namedModel &&
                  namedModel->property("displayName").toString() == customName && label && time &&
                  label->property("text").toString() == customName && readings.size() == 1 &&
                  time->property("text").toString() == readings.front().toMap().value(QStringLiteral("time")).toString(),
              "both panel widths render the complete custom clock name and its complete time");
        check(secondLabel->property("text").toString() == secondName &&
                  thirdLabel->property("text").toString() == thirdName,
              "renaming one clock leaves other clock labels unchanged");
        if (clock) {
            if (namedWidth > 0)
                check(std::abs(namedWidth - clock->width()) <= 0.5,
                      "custom names stay content-sized when the panel width changes");
            namedWidth = clock->width();
        }
    }
    namedSettings.insert(QStringLiteral("time_zone"), QStringLiteral("Asia/Tokyo"));
    check(host->setWidgetSettings(firstClockId, namedSettings, error) &&
              QMetaObject::invokeMethod(namedModel, "refreshNow", Qt::DirectConnection),
          "a named clock can change timezone and refresh its minute reading");
    checkIndependentPanelLayout(root, 1, 4);
    auto* namedClock = clockItem(root, firstClockId);
    auto* namedLabel = namedClock ? panelItem(namedClock, QStringLiteral("worldClockZone")) : nullptr;
    check(namedModel->property("displayName").toString() == customName && namedLabel &&
              namedLabel->property("text").toString() == customName &&
              host->configuration()[widgetRow(host, firstClockId)].settings.value(QStringLiteral("label")).toString() == customName,
          "timezone changes and minute refresh preserve the custom name in the model, panel and settings");
    namedSettings.insert(QStringLiteral("label"), QString{});
    check(host->setWidgetSettings(firstClockId, namedSettings, error),
          "a custom clock name can be cleared independently");
    checkIndependentPanelLayout(root, 1, 4);
    namedClock = clockItem(root, firstClockId);
    namedLabel = namedClock ? panelItem(namedClock, QStringLiteral("worldClockZone")) : nullptr;
    const QVariantList unnamedReadings = namedModel->property("clocks").toList();
    check(namedLabel && unnamedReadings.size() == 1 &&
              namedLabel->property("text").toString() == QStringLiteral("Tokyo") &&
              namedModel->property("displayName").toString() == QStringLiteral("Tokyo") &&
              secondLabel->property("text").toString() == secondName &&
              thirdLabel->property("text").toString() == thirdName,
          "clearing one custom name restores its default city name while preserving other labels");

    for (const auto& item : configuration) {
        if (item.typeId != QStringLiteral("world-clock")) continue;
        auto settings = item.settings;
        settings.insert(QStringLiteral("show_date"), true);
        check(host->setWidgetSettings(item.instanceId, settings, error),
              "each individual clock accepts optional dates independently");
    }
    for (const int panelWidth : {330, 640}) {
        panel.resize(panelWidth, 32);
        root->setWidth(panelWidth);
        checkIndependentPanelLayout(root, 1, 4);
        for (const auto& item : configuration) {
            if (item.typeId != QStringLiteral("world-clock")) continue;
            QObject* model = widgetModel(host, item.instanceId);
            const QVariantList readings = model ? model->property("clocks").toList() : QVariantList{};
            auto* clock = clockItem(root, item.instanceId);
            auto* time = clock ? panelItem(clock, QStringLiteral("worldClockTime")) : nullptr;
            check(readings.size() == 1 && time,
                  "each date-enabled widget still exposes exactly its own clock model and reading");
            if (readings.size() != 1 || !time) continue;
            const QVariantMap reading = readings.front().toMap();
            const QString expected = reading.value(QStringLiteral("date")).toString().mid(5) +
                                     QLatin1Char(' ') + reading.value(QStringLiteral("time")).toString();
            check(time->property("text").toString() == expected,
                  "narrow and wide panels preserve the full requested date and time");
        }
    }
    for (const auto& item : configuration) {
        if (item.typeId == QStringLiteral("world-clock"))
            check(host->setWidgetSettings(item.instanceId, item.settings, error),
                  "layout restores each clock's time-only configuration");
    }

    QJsonObject countdownSettings = configuration.front().settings;
    countdownSettings.insert(QStringLiteral("daily_repeat"), false);
    countdownSettings.insert(QStringLiteral("time_zone"), QStringLiteral("UTC"));
    countdownSettings.insert(QStringLiteral("target_date"), QDate::currentDate().addYears(5).toString(Qt::ISODate));
    check(host->setWidgetSettings(countdownId, countdownSettings, error),
          "layout accepts Countdown with more than two hour digits");
    panel.resize(330, 32);
    root->setWidth(330);
    checkIndependentPanelLayout(root, 1, 4);
    auto* display = panelItem(root, QStringLiteral("countdownDisplay"));
    check(display && display->property("text").toString().section(QLatin1Char(' '), -1)
                         .section(QLatin1Char(':'), 0, 0).size() > 2,
          "long-hour regression renders actual extended Countdown content");
    countdownSettings.insert(QStringLiteral("target_date"), QDate::currentDate().addDays(-1).toString(Qt::ISODate));
    check(host->setWidgetSettings(countdownId, countdownSettings, error),
          "layout accepts completed Countdown beside the independent clocks");
    checkIndependentPanelLayout(root, 1, 4);
    QObject* countdownModel = widgetModel(host, countdownId);
    check(countdownModel && countdownModel->property("status").toString() == QStringLiteral("completed"),
          "completion regression exercises the real completed Countdown model");

    check(host->moveWidget(secondClockId, 0, error), "an individual clock can move ahead of Countdown");
    checkIndependentPanelLayout(root, 1, 4);
    auto* movedClock = clockItem(root, secondClockId);
    auto* countdown = panelItem(root, QStringLiteral("countdownWidget"));
    check(movedClock && countdown && panelBounds(movedClock, root).right() <= panelBounds(countdown, root).left(),
          "rendered order follows configured order even when a clock precedes Countdown");
    check(host->moveWidget(countdownId, host->rowCount() - 1, error),
          "Countdown can move after all individual clock widgets");
    checkIndependentPanelLayout(root, 1, 4);
    countdown = panelItem(root, QStringLiteral("countdownWidget"));
    for (auto* clock : panelItems(root, QStringLiteral("worldClockWidget")))
        check(countdown && panelBounds(clock, root).right() <= panelBounds(countdown, root).left(),
              "Countdown is not forced before configured clocks");

    QObject* unaffectedModel = widgetModel(host, secondClockId);
    check(host->setVisible(firstClockId, false, error), "one clock can be hidden independently");
    checkIndependentPanelLayout(root, 1, 3);
    check(!clockItem(root, firstClockId) && widgetModel(host, secondClockId) == unaffectedModel,
          "hiding one clock preserves the other clock instance");
    check(host->setVisible(firstClockId, true, error) && host->setEnabled(firstClockId, false, error),
          "one visible clock can be disabled independently");
    checkIndependentPanelLayout(root, 1, 3);
    check(!clockItem(root, firstClockId) && widgetModel(host, secondClockId) == unaffectedModel,
          "disabling one clock leaves the other clock rendered with its existing model");
    check(host->setEnabled(firstClockId, true, error), "one clock can be re-enabled independently");
    checkIndependentPanelLayout(root, 1, 4);
    check(host->removeWidget(firstClockId, error), "one clock can be removed independently");
    checkIndependentPanelLayout(root, 1, 3);
    check(widgetModel(host, secondClockId) == unaffectedModel,
          "removing one clock preserves other clock models");

    check(host->setVisible(countdownId, false, error), "Countdown can be hidden without stretching clocks");
    checkIndependentPanelLayout(root, 0, 3);
    check(host->setVisible(countdownId, true, error), "Countdown can be restored beside remaining clocks");
    configuration = host->configuration();
    const auto firstCountdown = std::find_if(configuration.begin(), configuration.end(),
        [](const auto& item) { return item.typeId == QStringLiteral("countdown"); });
    if (firstCountdown != configuration.end()) {
        auto secondCountdown = *firstCountdown;
        secondCountdown.instanceId = QStringLiteral("countdown-layout-second");
        configuration.push_back(secondCountdown);
        check(host->setConfiguration(configuration, error), "layout accepts multiple independent Countdown instances");
        panel.resize(640, 32);
        root->setWidth(640);
        checkIndependentPanelLayout(root, 2, 3);
    }
    check(host->setConfiguration({}, error), "the independent widget panel can remove every module");
    checkIndependentPanelLayout(root, 0, 0);
    auto* emptyLabel = panelItem(root, QStringLiteral("panelEmptyLabel"));
    auto* settings = panelItem(root, QStringLiteral("worldClockSettingsButton"));
    int settingsRequests = 0;
    QObject::connect(&panel, &w11widget::PanelWindow::settingsRequested, &panel,
                     [&settingsRequests] { ++settingsRequests; });
    check(emptyLabel && emptyLabel->isVisible() && settings && settings->isVisible() &&
              QMetaObject::invokeMethod(settings, "activate", Qt::DirectConnection) && settingsRequests == 1,
          "the empty panel retains its shared Settings action for adding new clocks");
    check(!panel.isVisible() && !host->isRunning(),
          "all layout regressions keep the native panel hidden and module timers stopped");
    expectHiddenWindows();
}

bool rejectControlsKeepsEventLoopRunning(QDialog& dialog) {
    // A queued sentinel distinguishes keeping the application alive from a
    // rejected-controls handler calling quit(). All callbacks belong to this
    // local context, including when quit prevents the sentinel from running.
    QObject context;
    bool continued = false;
    QTimer::singleShot(0, &context, [&] {
        dialog.reject();
        QTimer::singleShot(0, &context, [&] {
            continued = true;
            QCoreApplication::exit(0);
        });
    });
    QCoreApplication::exec();
    return continued;
}

void testPanelStartupAndSettingsButton() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        check(false, "panel startup owns an isolated settings directory");
        return;
    }
    const QString path = temporary.filePath(QStringLiteral("panel-startup.json"));
    w11widget::PanelSettings saved;
    saved.widthDip = 450;
    saved.offsetDip = 170;
    saved.opacity = 0.73;
    QString error;
    if (!w11widget::SettingsStore(path).save(saved, error)) {
        check(false, "panel startup preferences fixture can be saved");
        return;
    }
    const QByteArray original = fixtureBytes(path);
    for (const bool availableTray : {true, false}) {
        Fixture fixture;
        auto services = fixture.services(path);
        int trayPresentations = 0;
        int settingsRequests = 0;
        int lifecycleChanges = 0;
        QSystemTrayIcon* suppressedTray = nullptr;
        services.trayAvailable = [availableTray] { return availableTray; };
        services.presentTray = [&](QSystemTrayIcon* tray) {
            ++trayPresentations;
            suppressedTray = tray;
        };
        w11widget::ApplicationController controller(nullptr, std::move(services));
        if (!controller.start(error, true)) {
            check(false, "ordinary interactive panel startup succeeds with fixture-only display");
            return;
        }
        Controls controls;
        if (!controls.find()) { check(false, "panel startup prepares hidden settings controls"); return; }
        controls.watchdog = controller.findChild<QTimer*>(QStringLiteral("safetyWatchdog"));
        w11widget::PanelWindow* panel = nullptr;
        for (QWindow* window : QGuiApplication::allWindows()) {
            if (auto* candidate = qobject_cast<w11widget::PanelWindow*>(window)) {
                panel = candidate;
                break;
            }
        }
        auto* host = panel ? panel->widgetHost() : nullptr;
        if (!panel || !panel->rootObject() || !host || !controls.watchdog) {
            check(false, "panel startup exposes its real hidden QML panel and module state");
            return;
        }
        panel->rootObject()->setProperty("width", 640);
        checkIndependentPanelLayout(panel->rootObject(), 1, 3);
        QQuickItem* worldClock = panelItem(panel->rootObject(), QStringLiteral("worldClockWidget"));
        check(worldClock && std::abs(worldClock->width() - worldClock->implicitWidth()) <= 1.0 &&
                  panelItems(worldClock, QStringLiteral("worldClockZone")).size() == 1,
              "each startup clock keeps its own natural width and timezone label");
        QObject* settingsButton = panelItem(panel->rootObject(), QStringLiteral("worldClockSettingsButton"));
        if (!settingsButton) { check(false, "panel contains its global Settings button"); return; }
        check(trayPresentations == 0 && !suppressedTray,
              "panel-only startup does not create a tray presentation");
        check(fixture.nativeCalls.size() == 1 && sameBounds(fixture.nativeCalls.front(), {170, 1040, 620, 1072}),
              "ordinary startup displays the currently measured saved placement exactly once");
        check(!controls.dialog->isVisible() && !controls.dialog->isMinimized() &&
                  controls.watchdog->isActive() && host->isRunning(),
              "successful panel startup keeps settings closed while maintenance and modules run");
        check(!panel->isVisible(), "panel startup recorder never displays an actual taskbar overlay");
        check(controls.width->value() == 450 && controls.offset->value() == 170 &&
              controls.opacity->value() == 73 && controls.confirmation->isChecked(),
              "panel startup preserves saved preferences and its current placement approval");
        if (fixture.nativeCalls.empty()) return;
        const auto shownBounds = fixture.nativeCalls.front();
        const auto hiddenPanelGeometry = panel->geometry();
        const auto configuration = host->configuration();
        std::vector<QObject*> models;
        std::vector<QVariant> lifecycleStates;
        for (int row = 0; row < host->rowCount(); ++row) {
            models.push_back(host->data(host->index(row, 0), w11widget::WidgetHostModel::ViewModelRole).value<QObject*>());
            lifecycleStates.push_back(host->data(host->index(row, 0), w11widget::WidgetHostModel::LifecycleStateRole));
        }
        QObject::connect(panel, &w11widget::PanelWindow::settingsRequested, &controller, [&] { ++settingsRequests; });
        QObject::connect(host, &w11widget::WidgetHostModel::runningChanged, &controller, [&] { ++lifecycleChanges; });
        const auto checkPanelUnchanged = [&] {
            check(fixture.nativeCalls.size() == 1 && sameBounds(fixture.nativeCalls.front(), shownBounds) &&
                  controls.watchdog->isActive() && host->isRunning() && lifecycleChanges == 0,
                  "Settings actions preserve the existing display request and running module lifecycle");
            for (const auto& bounds : fixture.retainCalls) {
                check(sameBounds(bounds, shownBounds), "Settings actions never move the approved panel rectangle");
            }
            check(!panel->isVisible() && panel->geometry() == hiddenPanelGeometry && host->configuration() == configuration,
                  "Settings actions preserve hidden fixture geometry and module configuration");
            check(host->rowCount() == static_cast<int>(models.size()), "Settings actions preserve module count");
            for (int row = 0; row < host->rowCount() && row < static_cast<int>(models.size()); ++row) {
                check(host->data(host->index(row, 0), w11widget::WidgetHostModel::ViewModelRole).value<QObject*>() == models[row] &&
                      host->data(host->index(row, 0), w11widget::WidgetHostModel::LifecycleStateRole) == lifecycleStates[row],
                      "Settings actions preserve each module instance and lifecycle state");
            }
        };
        const auto openFromPanelButton = [&] {
            const int previousRequests = settingsRequests;
            const auto previousRetains = fixture.retainCalls.size();
            check(QMetaObject::invokeMethod(settingsButton, "activate", Qt::DirectConnection),
                  "the global Settings button exposes its shared activation handler");
            check(settingsRequests == previousRequests + 1 && controls.dialog->isVisible() &&
                      !controls.dialog->isMinimized(),
                  "global Settings activation reopens the panel controls");
            check(fixture.retainCalls.size() == previousRetains,
                  "opening Settings does not request a native panel reposition");
            checkPanelUnchanged();
        };
        openFromPanelButton();
        check(rejectControlsKeepsEventLoopRunning(*controls.dialog),
              "rejecting Settings keeps a displayed panel alive with or without a tray");
        check(!controls.dialog->isVisible(), "rejecting Settings closes only the settings window");
        checkPanelUnchanged();
        openFromPanelButton();
        controls.show->click();
        check(!controls.dialog->isVisible(), "successful Show returns from reopened Settings to the panel");
        checkPanelUnchanged();
        check(fixtureBytes(path) == original, "panel startup and Settings actions preserve saved preference bytes");
    }
    expectHiddenWindows();
    check(fixtureBytes(path) == original, "panel startup shutdown without edits preserves saved preferences");

    {
        Fixture invalidPlacement;
        invalidPlacement.snapshot.bounds = {0, 1032, 40, 1080};
        auto services = invalidPlacement.services(path);
        int trayPresentations = 0;
        services.trayAvailable = [] { return true; };
        services.presentTray = [&](QSystemTrayIcon*) { ++trayPresentations; };
        w11widget::ApplicationController controller(nullptr, std::move(services));
        if (!controller.start(error, true)) { check(false, "invalid placement retains an initialized recovery controller"); return; }
        Controls controls;
        if (!controls.find()) { check(false, "invalid placement exposes recovery controls"); return; }
        controls.watchdog = controller.findChild<QTimer*>(QStringLiteral("safetyWatchdog"));
        auto* host = findWidgetHost();
        const QString startupDiagnostic = controls.status->text();
        check(!controls.dialog->isVisible() && !startupDiagnostic.isEmpty() &&
                  startupDiagnostic == controls.preview->text() && trayPresentations == 0,
              "invalid startup geometry keeps settings hidden and retains its placement diagnostic without a tray");
        check(invalidPlacement.nativeCalls.empty() && invalidPlacement.retainCalls.empty() &&
              controls.watchdog && !controls.watchdog->isActive() && host && !host->isRunning(),
              "invalid startup geometry neither displays a panel nor starts orphaned maintenance or widgets");
        controller.activateControls();
        QCoreApplication::processEvents();
        check(controls.dialog->isVisible() && !controls.dialog->isMinimized() &&
                  controls.status->text() == startupDiagnostic && trayPresentations == 0 &&
                  invalidPlacement.nativeCalls.empty() && invalidPlacement.retainCalls.empty() &&
                  controls.watchdog && !controls.watchdog->isActive() && host && !host->isRunning(),
              "explicit settings activation reveals invalid-placement recovery without displaying a panel or starting timers");
        check(fixtureBytes(path) == original, "invalid startup geometry preserves saved preference bytes");
    }
    expectHiddenWindows();
}

void testInteractiveStartupAndActivation() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        check(false, "interactive startup owns an isolated settings directory");
        return;
    }
    const QString path = temporary.filePath(QStringLiteral("interactive.json"));
    w11widget::PanelSettings saved;
    saved.widthDip = 450;
    saved.offsetDip = 170;
    saved.opacity = 0.73;
    QString error;
    if (!w11widget::SettingsStore(path).save(saved, error)) {
        check(false, "interactive startup preferences fixture can be saved");
        return;
    }
    const QByteArray original = fixtureBytes(path);
    Fixture fixture;
    {
        auto services = fixture.services(path);
        int trayPresentations = 0;
        QSystemTrayIcon* suppressedTray = nullptr;
        services.trayAvailable = [] { return true; };
        services.presentTray = [&](QSystemTrayIcon* tray) {
            ++trayPresentations;
            suppressedTray = tray;
        };
        w11widget::ApplicationController controller(nullptr, std::move(services));
        if (!controller.start(error, true, w11widget::ApplicationController::StartupMode::Settings)) {
            std::cerr << "Interactive startup failed: " << error.toStdString() << '\n';
            check(false, "interactive startup succeeds without a real tray or taskbar probe");
            return;
        }
        Controls controls;
        if (!controls.find()) { check(false, "interactive startup creates its own controls"); return; }
        controls.watchdog = controller.findChild<QTimer*>(QStringLiteral("safetyWatchdog"));
        auto* host = findWidgetHost();
        if (!controls.watchdog || !host) {
            check(false, "interactive startup exposes its visibility and module state");
            return;
        }
        QCoreApplication::processEvents();
        check(trayPresentations == 0 && !suppressedTray,
              "explicit settings startup never creates or presents a tray even when the system supports one");
        check(controls.dialog->isVisible() && !controls.dialog->isMinimized(),
              "explicit settings startup presents controls even with an available tray");
        check(fixture.nativeCalls.empty() && fixture.retainCalls.empty() &&
              !controls.watchdog->isActive() && !host->isRunning(),
              "showing startup controls never restores panel visibility or starts widgets");
        check(controls.width->value() == 450 && controls.offset->value() == 170 &&
              controls.opacity->value() == 73,
              "visible startup controls preserve the user's saved placement and opacity");
        const auto checkOverlayHidden = [] {
            for (QWindow* window : QGuiApplication::allWindows()) {
                if (const auto* panel = qobject_cast<w11widget::PanelWindow*>(window)) {
                    check(!panel->isVisible(), "interactive fixture never displays a taskbar overlay");
                }
            }
        };
        checkOverlayHidden();

        controls.dialog->showMinimized();
        QCoreApplication::processEvents();
        check(controls.dialog->isMinimized(), "owned controls are minimized before activation regression");
        controller.activateControls();
        QCoreApplication::processEvents();
        check(controls.dialog->isVisible() && !controls.dialog->isMinimized(),
              "activation restores minimized controls to a visible normal window");
        controls.dialog->hide();
        check(!controls.dialog->isVisible(), "owned controls can be hidden before relaunch activation");
        check(QMetaObject::invokeMethod(&controller, "activateControls", Qt::DirectConnection),
              "relaunch activation has a public Qt slot entry point");
        QCoreApplication::processEvents();
        check(controls.dialog->isVisible() && !controls.dialog->isMinimized() &&
              fixture.nativeCalls.empty() && fixture.retainCalls.empty() &&
              !controls.watchdog->isActive() && !host->isRunning(),
              "relaunch reveals hidden controls without automatically displaying the panel");

        // This calls only the injected display recorder. It never displays the
        // real panel or uses the fixture taskbar identity in a native API.
        controls.confirmation->setChecked(true);
        controls.show->click();
        check(fixture.nativeCalls.size() == 1 && controls.watchdog->isActive() && host->isRunning(),
              "only explicit Show establishes requested panel visibility");
        check(!controls.dialog->isVisible(), "successful explicit Show returns from settings to the panel");
        if (!fixture.nativeCalls.empty()) {
            const auto shownBounds = fixture.nativeCalls.back();
            controls.dialog->hide();
            controller.activateControls();
            QCoreApplication::processEvents();
            check(controls.dialog->isVisible() && fixture.nativeCalls.size() == 1 &&
                  sameBounds(fixture.nativeCalls.back(), shownBounds) &&
                  controls.watchdog->isActive() && host->isRunning(),
                  "activation preserves existing Show intent and its approved rectangle");
        }
        checkOverlayHidden();
        controls.hide->click();
        check(!controls.watchdog->isActive() && !host->isRunning(),
              "explicit Hide remains authoritative after controls activation");
        check(fixtureBytes(path) == original, "controls activation does not rewrite user preferences");
    }
    expectHiddenWindows();
    check(fixtureBytes(path) == original, "interactive shutdown without edits preserves fixture bytes");

    {
        Fixture backgroundFixture;
        auto services = backgroundFixture.services(path);
        int trayPresentations = 0;
        QSystemTrayIcon* suppressedTray = nullptr;
        services.trayAvailable = [] { return true; };
        services.presentTray = [&](QSystemTrayIcon* tray) {
            ++trayPresentations;
            suppressedTray = tray;
        };
        w11widget::ApplicationController controller(nullptr, std::move(services));
        if (!controller.start(error, true, w11widget::ApplicationController::StartupMode::Background)) {
            check(false, "explicit background startup succeeds with an available tray");
            return;
        }
        Controls controls;
        if (!controls.find()) { check(false, "background startup prepares hidden controls"); return; }
        controls.watchdog = controller.findChild<QTimer*>(QStringLiteral("safetyWatchdog"));
        auto* host = findWidgetHost();
        QCoreApplication::processEvents();
        check(trayPresentations == 0 && !suppressedTray && !controls.dialog->isVisible() &&
              backgroundFixture.nativeCalls.empty() && backgroundFixture.retainCalls.empty() &&
              controls.watchdog && !controls.watchdog->isActive() && host && !host->isRunning(),
              "explicit background startup keeps controls and panel hidden without creating a tray or starting timers");
        controller.activateControls();
        QCoreApplication::processEvents();
        check(controls.dialog->isVisible() && !controls.dialog->isMinimized() &&
              trayPresentations == 0 && !suppressedTray && backgroundFixture.nativeCalls.empty() &&
              backgroundFixture.retainCalls.empty() && controls.watchdog &&
              !controls.watchdog->isActive() && host && !host->isRunning(),
              "explicit settings activation reveals background controls without showing the overlay or starting timers");
    }
    expectHiddenWindows();

    {
        Fixture backgroundFixture;
        auto services = backgroundFixture.services(path);
        int trayPresentations = 0;
        services.trayAvailable = [] { return false; };
        services.presentTray = [&](QSystemTrayIcon*) { ++trayPresentations; };
        w11widget::ApplicationController controller(nullptr, std::move(services));
        if (!controller.start(error, true, w11widget::ApplicationController::StartupMode::Background)) {
            check(false, "explicit background startup can initialize with no tray");
            return;
        }
        Controls controls;
        if (!controls.find()) { check(false, "tray-unavailable background startup prepares hidden controls"); return; }
        controls.watchdog = controller.findChild<QTimer*>(QStringLiteral("safetyWatchdog"));
        auto* host = findWidgetHost();
        QCoreApplication::processEvents();
        check(!controls.dialog->isVisible() && trayPresentations == 0 && backgroundFixture.nativeCalls.empty() &&
              backgroundFixture.retainCalls.empty() && controls.watchdog &&
              !controls.watchdog->isActive() && host && !host->isRunning(),
              "background startup stays hidden without a tray and does not start panel maintenance or widgets");
        controller.activateControls();
        QCoreApplication::processEvents();
        check(controls.dialog->isVisible() && !controls.dialog->isMinimized() && trayPresentations == 0 &&
              backgroundFixture.nativeCalls.empty() && backgroundFixture.retainCalls.empty() &&
              controls.watchdog && !controls.watchdog->isActive() && host && !host->isRunning(),
              "explicit settings activation opens no-tray background controls without displaying a panel or starting timers");
        check(!rejectControlsKeepsEventLoopRunning(*controls.dialog),
              "closing explicitly opened controls quits when no panel is displayed");
    }
    expectHiddenWindows();
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QQuickWindow::setDefaultAlphaBuffer(true);
    if (application.arguments().contains(QStringLiteral("--interactive-startup-regression"))) {
        // Opt-in only: show short-lived controls on this process's desktop. No
        // tray icons, actual taskbar overlays or real-profile settings are used.
        testPanelStartupAndSettingsButton();
        testInteractiveStartupAndActivation();
        std::cout << "Interactive startup regression: " << (failures == 0 ? "PASS" : "FAIL") << '\n';
        return failures == 0 ? 0 : 1;
    }
    testButtonAndCalibration();
    testNativeFailureFeedback();
    testProbeFailureFeedback();
    testChangedEnvironment();
    testPinnedFullscreenShow();
    testPinnedMaintenanceAndManualHide();
    testPinnedProbeLoss();
    testPinnedGeometryChanges();
    testMissingSelectedMonitorNeverFallsBackToPrimary();
    testPinnedShellRecreation();
    testPinnedPowerEvents();
    testPendingCalibrationKeepsPinnedRectangle();
    testExistingShowMaintainsCachedApproval();
    testPreferencesPersistenceAndReset();
    testLegacyFullscreenPreferenceCannotHidePinnedPanel();
    testMalformedSettingsPreservation();
    testPendingPreferencesFlushAtShutdown();
    testFailedSaveRemainsDirty();
    testNativeMaintenanceFailureKeepsPinned();
    testFailedRepositionKeepsPinned();
    testOwnWindowEventsDuringShow();
    testPinnedInvalidPlacementKeepsObservedRectangle();
    testWidgetLifecycleAndPlacementIndependence();
    testWidgetPreferencesSurvivePanelEditsAndRestart();
    testIndependentClockLifecycleAndPlacement();
    testIndependentClockPanelLayout();
    if (failures != 0) {
        std::cerr << failures << " controller interaction checks failed.\n";
        return 1;
    }
    std::cout << "Controller interaction checks passed.\n";
    return 0;
}
