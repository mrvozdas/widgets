#include "ui/Strings.h"

#include "app/ApplicationController.h"
#include "core/VisibilityPolicy.h"
#include "core/WidgetHostModel.h"
#include "ui/WidgetSettingsDialog.h"
#include "ui/FluentStyle.h"
#include "ui/PlacementPreview.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace w11widget {
namespace {
QIcon appIcon() {
    QPixmap image(32, 32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#18232c"));
    painter.drawRoundedRect(QRectF(1, 1, 30, 30), 8, 8);
    painter.setBrush(QColor("#72e0bb"));
    painter.drawRoundedRect(QRectF(7, 8, 5, 16), 2, 2);
    painter.drawRoundedRect(QRectF(15, 8, 10, 6), 2, 2);
    painter.drawRoundedRect(QRectF(15, 18, 10, 6), 2, 2);
    return QIcon(image);
}

bool sameRect(const Rect& a, const Rect& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool sameGeometry(const windows::TaskbarSnapshot& a, const windows::TaskbarSnapshot& b) {
    // Auto-hide is temporary state, not approval of a different rectangle.
    return a.hwnd == b.hwnd && a.dpi == b.dpi && a.edge == b.edge &&
        sameRect(a.bounds, b.bounds) && sameRect(a.monitorBounds, b.monitorBounds) &&
        a.monitorDevice == b.monitorDevice;
}
} // namespace

ApplicationController::ApplicationController(QObject* parent, ControllerServices services)
    : QObject(parent), services_(std::move(services)) {
    if (!services_.readTaskbar) services_.readTaskbar = windows::readTaskbar;
    if (!services_.readTaskbarForMonitor) {
        services_.readTaskbarForMonitor = [this](const QString& device, std::string& error) {
            return device == QStringLiteral("primary") ? services_.readTaskbar(error) :
                windows::readTaskbarForMonitor(device.toStdWString(), error);
        };
    }
    if (!services_.monitors) services_.monitors = windows::enumerateMonitors;
    if (!services_.trayAvailable) services_.trayAvailable = QSystemTrayIcon::isSystemTrayAvailable;
    if (!services_.presentTray) services_.presentTray = [](QSystemTrayIcon* tray) { tray->show(); };
    if (!services_.showPanel) {
        services_.showPanel = [this](const Rect& bounds, QString& error) {
            return panel_.showAt(bounds, error);
        };
    }
    if (!services_.retainPanel) {
        services_.retainPanel = [this](const Rect& bounds, QString& error) {
            return panel_.keepVisibleAt(bounds, error);
        };
    }
}

ApplicationController::~ApplicationController() {
    if (preferencesDirty_) savePreferences();
    if (safetyTimer_) safetyTimer_->stop();
    if (environmentTimer_) environmentTimer_->stop();
    disconnect(&panel_, nullptr, this, nullptr);
    panel_.hide();
}

bool ApplicationController::start(QString& error, bool interactive, StartupMode mode) {
    interactive_ = interactive;
    if (!panel_.initialize(error)) return false;
    QApplication::setWindowIcon(appIcon());

    PanelSettings loaded;
    QString loadMessage;
    if (interactive_ || !services_.settingsPath.isEmpty()) {
        settingsStore_ = std::make_unique<SettingsStore>(services_.settingsPath.isEmpty() ?
            SettingsStore::defaultPath() : services_.settingsPath);
        const auto result = settingsStore_->load();
        loaded = result.settings;
        loadMessage = result.ok ?
            (result.missing ? uiText("ui.degisiklikler_otomatik_kaydedilecek") :
                              uiText("ui.kayitli_ayarlar_yuklendi")) :
            uiText("ui.ayar_dosyasi_okunamadi_varsayilanlar_kullaniliyor") + result.error;
    }

    safetyTimer_ = new QTimer(this);
    safetyTimer_->setObjectName(QStringLiteral("safetyWatchdog"));
    // Native shell/window events are primary; this only repairs missed notifications.
    safetyTimer_->setTimerType(Qt::VeryCoarseTimer);
    safetyTimer_->setInterval(5000);
    connect(safetyTimer_, &QTimer::timeout, this, &ApplicationController::reevaluateVisibility);
    environmentTimer_ = new QTimer(this);
    environmentTimer_->setSingleShot(true);
    environmentTimer_->setInterval(150);
    connect(environmentTimer_, &QTimer::timeout, this, [this] {
        populateMonitors();
        if (requestedVisible_) reevaluateVisibility();
        else { refreshPreview(); updateReadinessStatus(); }
    });
    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(350);
    connect(saveTimer_, &QTimer::timeout, this, &ApplicationController::savePreferences);

    createControls();
    connect(panel_.widgetHost(), &WidgetHostModel::configurationChanged, this, [this] {
        if (applyingPreferences_ || !settingsStore_) return;
        preferencesDirty_ = true;
        saveTimer_->start();
    });
    applyPreferences(loaded);
    settingsStatus_->setText(loadMessage);
    connect(&panel_, &PanelWindow::settingsRequested, this, &ApplicationController::showControls);
    connect(&panel_, &PanelWindow::hideRequested, this, &ApplicationController::hidePanel);
    connect(&panel_, &PanelWindow::environmentChanged, this, &ApplicationController::handleEnvironmentChange);
    connect(&panel_, &PanelWindow::powerStateChanged, this, &ApplicationController::handlePowerState);
    connect(&panel_, &PanelWindow::shellRecreated, this, &ApplicationController::handleShellRecreation);
    connect(&panel_, &PanelWindow::visibilityMaintenanceFailed, this, [this](const QString& error) {
        setStatus(uiText("ui.panel_gorunurluk_korumasi") + error, true);
    });
    connect(&panel_, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible && requestedVisible_) environmentTimer_->start(0);
    });
    connect(&panel_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
        if (visibility == QWindow::Minimized && requestedVisible_) environmentTimer_->start(0);
    });
    if (interactive_) {
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] {
            if (requestedVisible_) environmentTimer_->start(0);
        });
        for (auto* screen : QGuiApplication::screens()) connectScreen(screen);
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
            connectScreen(screen);
            handleEnvironmentChange();
        });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this] { handleEnvironmentChange(); });
        connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this] { handleEnvironmentChange(); });
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            if (preferencesDirty_) savePreferences();
        });
    }
    refreshPreview();
    confirmed_->setChecked(true);
    updateReadinessStatus();
    if (interactive_) {
        if (mode == StartupMode::Panel) {
            activatePanel();
        } else if (mode == StartupMode::Settings) {
            activateControls();
        }
    }
    return true;
}

void ApplicationController::createControls() {
    controls_ = std::make_unique<QDialog>();
    controls_->setObjectName(QStringLiteral("panelControls"));
    controls_->setWindowTitle(uiText("ui.w11widget_1_ayarlar")
                                 .arg(QCoreApplication::applicationVersion()));
    controls_->setMinimumSize(880, 620);
    controls_->resize(980, 720);
    applyFluentStyle(controls_.get());
    auto* rootLayout = new QVBoxLayout(controls_.get());
    rootLayout->setContentsMargins(20, 20, 20, 16);
    rootLayout->setSpacing(14);
    auto* content = new QHBoxLayout;
    content->setSpacing(24);
    rootLayout->addLayout(content, 1);

    auto* navigation = new QWidget(controls_.get());
    navigation->setFixedWidth(170);
    auto* navigationLayout = new QVBoxLayout(navigation);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(6);
    auto* brand = new QLabel(QStringLiteral("w11widget"), navigation);
    brand->setProperty("heading", true);
    navigationLayout->addWidget(brand);
    auto* version = new QLabel(QCoreApplication::applicationVersion(), navigation);
    version->setProperty("muted", true);
    navigationLayout->addWidget(version);
    navigationLayout->addSpacing(22);
    auto* navigationGroup = new QButtonGroup(navigation);
    const auto addNavigation = [&](const QString& text, const QString& name) {
        auto* button = new QPushButton(text, navigation);
        button->setObjectName(name);
        button->setProperty("nav", true);
        button->setCheckable(true);
        navigationGroup->addButton(button);
        navigationLayout->addWidget(button);
        return button;
    };
    widgetNavigation_ = addNavigation(uiText("ui.widgetlar"), QStringLiteral("manageWidgets"));
    layoutNavigation_ = addNavigation(uiText("ui.yerlesim"), QStringLiteral("layoutNavigation"));
    auto* appearanceNavigation = addNavigation(uiText("ui.gorunum"), QStringLiteral("appearanceNavigation"));
    layoutNavigation_->setChecked(true);
    navigationLayout->addStretch();
    auto* navigationHint = new QLabel(uiText("ui.gorev_cubugunda_kucuk_bir_alan_ihtiyacin_olan_bilgiler"), navigation);
    navigationHint->setWordWrap(true);
    navigationLayout->addWidget(navigationHint);
    content->addWidget(navigation);

    controlPages_ = new QStackedWidget(controls_.get());
    controlPages_->setObjectName(QStringLiteral("controlPages"));
    content->addWidget(controlPages_, 1);
    panelControlsPage_ = new QWidget(controlPages_);
    panelControlsPage_->setObjectName(QStringLiteral("panelControlsPage"));
    controlPages_->addWidget(panelControlsPage_);
    auto* pageLayout = new QVBoxLayout(panelControlsPage_);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* placementScroll = new QScrollArea(panelControlsPage_);
    placementScroll->setWidgetResizable(true);
    auto* placementContent = new QWidget(placementScroll);
    placementScroll->setWidget(placementContent);
    pageLayout->addWidget(placementScroll);
    auto* layout = new QVBoxLayout(placementContent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* title = new QLabel(uiText("ui.yerlesim"), panelControlsPage_);
    title->setProperty("heading", true);
    layout->addWidget(title);
    auto* intro = new QLabel(uiText("ui.panelin_ekranini_ve_gorev_cubugundaki_yerini_sec"), panelControlsPage_);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    placementCanvas_ = new PlacementPreview(panelControlsPage_);
    placementCanvas_->setAccessibleName(uiText("ui.secilen_panel_konumunun_gorev_cubuguna_gore_onizlemesi"));
    layout->addWidget(placementCanvas_);
    preview_ = new QLabel(panelControlsPage_);
    preview_->setObjectName(QStringLiteral("placementPreview"));
    preview_->setWordWrap(true);
    layout->addWidget(preview_);

    auto* presets = new QHBoxLayout;
    auto* compactLeft = new QPushButton(uiText("ui.sola_yerlestir"), panelControlsPage_);
    compactLeft->setObjectName(QStringLiteral("placementPresetLeft"));
    auto* compactRight = new QPushButton(uiText("ui.saga_yerlestir"), panelControlsPage_);
    compactRight->setObjectName(QStringLiteral("placementPresetRight"));
    presets->addWidget(compactLeft);
    presets->addWidget(compactRight);
    presets->addStretch();
    layout->addLayout(presets);

    auto* placementCard = new QFrame(panelControlsPage_);
    placementCard->setProperty("card", true);
    auto* form = new QFormLayout(placementCard);
    form->setContentsMargins(16, 12, 16, 12);
    form->setSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    monitor_ = new QComboBox;
    monitor_->setObjectName(QStringLiteral("panelMonitor"));
    monitor_->addItem(uiText("ui.birincil_ekran"), QStringLiteral("primary"));
    side_ = new QComboBox;
    side_->setObjectName(QStringLiteral("panelSide"));
    side_->addItems({uiText("ui.sol"), uiText("ui.sag")});
    width_ = new QSpinBox;
    width_->setObjectName(QStringLiteral("panelWidth"));
    width_->setRange(80, 640);
    width_->setValue(330);
    width_->setSuffix(QStringLiteral(" px"));
    offset_ = new QSpinBox;
    offset_->setObjectName(QStringLiteral("panelOffset"));
    offset_->setRange(0, 20000);
    offset_->setValue(150);
    offset_->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("&Ekran"), monitor_);
    form->addRow(QStringLiteral("&Hizalama"), side_);
    form->addRow(uiText("ui.genislik"), width_);
    form->addRow(uiText("ui.kenar_boslugu"), offset_);
    layout->addWidget(placementCard);
    auto* placementHint = new QLabel(uiText("ui.onizleme_simgelerin_yerini_algilamaz_panelin_baslat_uygu"), panelControlsPage_);
    placementHint->setWordWrap(true);
    layout->addWidget(placementHint);
    confirmed_ = new QCheckBox(uiText("ui.bu_bolge_bos_baslat_uygulama_simgeleri_veya_saati_kapatm"));
    confirmed_->setObjectName(QStringLiteral("confirmRegion"));
    layout->addWidget(confirmed_);
    layout->addStretch();

    appearancePage_ = new QWidget(controlPages_);
    appearancePage_->setObjectName(QStringLiteral("appearancePage"));
    controlPages_->addWidget(appearancePage_);
    auto* appearanceLayout = new QVBoxLayout(appearancePage_);
    appearanceLayout->setContentsMargins(0, 0, 0, 0);
    appearanceLayout->setSpacing(14);
    auto* appearanceTitle = new QLabel(uiText("ui.gorunum"), appearancePage_);
    appearanceTitle->setProperty("heading", true);
    appearanceLayout->addWidget(appearanceTitle);
    auto* appearanceIntro = new QLabel(uiText("ui.ayarlar_windowsun_acik_veya_koyu_temasini_izler_panelin"), appearancePage_);
    appearanceIntro->setWordWrap(true);
    appearanceLayout->addWidget(appearanceIntro);
    auto* appearanceCard = new QFrame(appearancePage_);
    appearanceCard->setProperty("card", true);
    auto* appearanceForm = new QFormLayout(appearanceCard);
    appearanceForm->setContentsMargins(16, 16, 16, 16);
    opacity_ = new QSpinBox;
    opacity_->setObjectName(QStringLiteral("panelOpacity"));
    opacity_->setRange(25, 100);
    opacity_->setValue(95);
    opacity_->setSuffix(QStringLiteral("%"));
    appearanceForm->addRow(uiText("ui.panel_opakligi"), opacity_);
    appearanceLayout->addWidget(appearanceCard);
    auto* visibilityInfo = new QLabel(uiText("ui.panel_gosterden_sonra_gorunur_kalir_gizle_paneli_kapatir"), appearancePage_);
    visibilityInfo->setWordWrap(true);
    appearanceLayout->addWidget(visibilityInfo);
    appearanceLayout->addStretch();
    auto* maintenanceTitle = new QLabel(uiText("ui.bakim"), appearancePage_);
    maintenanceTitle->setProperty("section", true);
    appearanceLayout->addWidget(maintenanceTitle);
    auto* maintenance = new QHBoxLayout;
    auto* diagnostics = new QPushButton(uiText("ui.tanilama"));
    auto* reset = new QPushButton(uiText("ui.varsayilanlara_don"));
    reset->setObjectName(QStringLiteral("resetPreferences"));
    maintenance->addWidget(diagnostics);
    maintenance->addWidget(reset);
    maintenance->addStretch();
    appearanceLayout->addLayout(maintenance);

    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("panelStatus"));
    status_->setWordWrap(true);
    rootLayout->addWidget(status_);
    auto* footer = new QHBoxLayout;
    settingsStatus_ = new QLabel;
    settingsStatus_->setObjectName(QStringLiteral("settingsStatus"));
    settingsStatus_->setWordWrap(true);
    footer->addWidget(settingsStatus_, 1);
    show_ = new QPushButton(uiText("ui.goster"));
    show_->setObjectName(QStringLiteral("showPanel"));
    show_->setProperty("primary", true);
    show_->setAutoDefault(false);
    auto* hide = new QPushButton(uiText("ui.g_izle"));
    hide->setObjectName(QStringLiteral("hidePanel"));
    auto* exit = new QPushButton(uiText("ui.cikis"));
    for (auto* button : {hide, exit, show_}) footer->addWidget(button);
    rootLayout->addLayout(footer);
    connect(widgetNavigation_, &QPushButton::clicked, this, &ApplicationController::showWidgetSettings);
    connect(layoutNavigation_, &QPushButton::clicked, this, &ApplicationController::showControls);
    connect(appearanceNavigation, &QPushButton::clicked, this, [this] {
        controlPages_->setCurrentWidget(appearancePage_);
    });
    const auto applyPreset = [this](int side) {
        const QSignalBlocker blockSide(side_);
        const QSignalBlocker blockWidth(width_);
        const QSignalBlocker blockOffset(offset_);
        side_->setCurrentIndex(side);
        width_->setValue(330);
        offset_->setValue(150);
        invalidatePlacement();
        if (settingsStore_) { preferencesDirty_ = true; saveTimer_->start(); }
    };
    connect(compactLeft, &QPushButton::clicked, this, [applyPreset] { applyPreset(0); });
    connect(compactRight, &QPushButton::clicked, this, [applyPreset] { applyPreset(1); });
    connect(show_, &QPushButton::clicked, this, &ApplicationController::showPanel);
    connect(hide, &QPushButton::clicked, this, &ApplicationController::hidePanel);
    connect(diagnostics, &QPushButton::clicked, this, &ApplicationController::showDiagnostics);
    connect(reset, &QPushButton::clicked, this, &ApplicationController::resetPreferences);
    connect(exit, &QPushButton::clicked, qApp, &QCoreApplication::quit);
    connect(controls_.get(), &QDialog::rejected, this, [this] {
        // A displayed panel has its own Settings button, even without a tray.
        if (!tray_ && !displayed_) { hidePanel(); QCoreApplication::quit(); }
    });
    const auto geometryChanged = [this] {
        if (applyingPreferences_) return;
        invalidatePlacement();
        if (settingsStore_) { preferencesDirty_ = true; saveTimer_->start(); }
    };
    connect(monitor_, &QComboBox::currentIndexChanged, this, geometryChanged);
    connect(side_, &QComboBox::currentIndexChanged, this, geometryChanged);
    connect(width_, &QSpinBox::valueChanged, this, geometryChanged);
    connect(offset_, &QSpinBox::valueChanged, this, geometryChanged);
    const auto appearanceChanged = [this] {
        if (applyingPreferences_) return;
        panel_.setOpacity(opacity_->value() / 100.0);
        if (settingsStore_) { preferencesDirty_ = true; saveTimer_->start(); }
        if (requestedVisible_) reevaluateVisibility();
    };
    connect(opacity_, &QSpinBox::valueChanged, this, appearanceChanged);
    connect(confirmed_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            approvedSnapshot_.reset();
        }
        refreshPreview();
        if (confirmed_->isChecked() && previewSnapshot_ && previewError_.isEmpty()) {
            approvedSnapshot_ = previewSnapshot_;
        }
        updateReadinessStatus();
    });
    // Enter commits the focused editor instead of unexpectedly showing or hiding the panel.
    for (auto* button : controls_->findChildren<QPushButton*>()) button->setAutoDefault(false);
}

void ApplicationController::populateMonitors() {
    const QString selected = monitor_->currentData().toString();
    const QSignalBlocker blocker(monitor_);
    monitor_->clear();
    monitor_->addItem(uiText("ui.birincil_ekran"), QStringLiteral("primary"));
    std::string error;
    for (const auto& monitor : services_.monitors(error)) {
        const QString device = QString::fromStdWString(monitor.device);
        const QString label = uiText("ui.1_2_3_4").arg(device)
            .arg(monitor.bounds.width()).arg(monitor.bounds.height())
            .arg(monitor.primary ? QStringLiteral(" (birincil)") : QString());
        monitor_->addItem(label, device);
    }
    int index = monitor_->findData(selected);
    if (index < 0 && !selected.isEmpty()) {
        monitor_->addItem(selected + uiText("ui.bagli_degil"), selected);
        index = monitor_->count() - 1;
    }
    monitor_->setCurrentIndex(index < 0 ? 0 : index);
}

void ApplicationController::applyPreferences(const PanelSettings& settings) {
    applyingPreferences_ = true;
    populateMonitors();
    int index = monitor_->findData(settings.monitor);
    if (index < 0) {
        monitor_->addItem(settings.monitor + uiText("ui.bagli_degil"), settings.monitor);
        index = monitor_->count() - 1;
    }
    monitor_->setCurrentIndex(index);
    side_->setCurrentIndex(settings.side == PanelSide::Left ? 0 : 1);
    width_->setValue(settings.widthDip);
    offset_->setValue(settings.offsetDip);
    opacity_->setValue(static_cast<int>(std::lround(settings.opacity * 100)));
    panel_.setOpacity(opacity_->value() / 100.0);
    QString widgetError;
    if (!panel_.widgetHost()->setConfiguration(settings.widgets, widgetError)) {
        setStatus(uiText("ui.widget_ayarlari_uygulanamadi") + widgetError, true);
    }
    applyingPreferences_ = false;
}

PanelSettings ApplicationController::preferences() const {
    PanelSettings settings;
    settings.monitor = monitor_->currentData().toString();
    settings.side = side_->currentIndex() == 0 ? PanelSide::Left : PanelSide::Right;
    settings.widthDip = width_->value();
    settings.offsetDip = offset_->value();
    settings.opacity = opacity_->value() / 100.0;
    settings.hideForFullscreen = false;
    settings.widgets = panel_.widgetHost()->configuration();
    return settings;
}

void ApplicationController::savePreferences() {
    saveTimer_->stop();
    if (!settingsStore_) return;
    QString error;
    const bool saved = settingsStore_->save(preferences(), error);
    preferencesDirty_ = !saved;
    settingsStatus_->setText(saved ? uiText("ui.ayarlar_kaydedildi") :
        uiText("ui.ayarlar_kaydedilemedi") + error);
    settingsStatus_->setToolTip(settingsStore_->filePath());
}

void ApplicationController::resetPreferences() {
    hidePanel();
    clearConfirmation();
    applyPreferences(PanelSettings{});
    refreshPreview();
    preferencesDirty_ = true;
    savePreferences();
    setStatus(uiText("ui.varsayilan_ayarlara_donuldu_bolgeyi_yeniden_onayla"));
}

void ApplicationController::connectScreen(QScreen* screen) {
    connect(screen, &QScreen::geometryChanged, this, [this] { handleEnvironmentChange(); });
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { handleEnvironmentChange(); });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { handleEnvironmentChange(); });
}

void ApplicationController::createTray() {
    if (!services_.trayAvailable()) return;
    trayMenu_ = std::make_unique<QMenu>();
    trayMenu_->addAction(uiText("ui.paneli_goster"), this, &ApplicationController::showPanel);
    trayMenu_->addAction(uiText("ui.paneli_gizle"), this, &ApplicationController::hidePanel);
    trayMenu_->addAction(QStringLiteral("Ayarlar…"), this, &ApplicationController::showControls);
    trayMenu_->addAction(uiText("ui.widget_lari_duzenle"), this, &ApplicationController::showWidgetSettings);
    trayMenu_->addAction(uiText("ui.tanilama_2"), this, &ApplicationController::showDiagnostics);
    trayMenu_->addSeparator();
    trayMenu_->addAction(uiText("ui.cikis_2"), qApp, &QCoreApplication::quit);
    tray_ = std::make_unique<QSystemTrayIcon>(appIcon());
    tray_->setToolTip(uiText("ui.w11widget_panel_kontrolu"));
    tray_->setContextMenu(trayMenu_.get());
    connect(tray_.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) showControls();
    });
    services_.presentTray(tray_.get());
}

PlacementRequest ApplicationController::request() const {
    PlacementRequest placement;
    placement.side = side_->currentIndex() == 0 ? PanelSide::Left : PanelSide::Right;
    placement.widthDip = width_->value();
    placement.heightDip = 32;
    placement.offsetDip = offset_->value();
    placement.regionConfirmed = confirmed_->isChecked();
    return placement;
}

std::optional<windows::TaskbarSnapshot> ApplicationController::probeTaskbar(std::string& error) const {
    return services_.readTaskbarForMonitor(monitor_->currentData().toString(), error);
}

void ApplicationController::refreshPreview() {
    std::string error;
    const auto snapshot = probeTaskbar(error);
    previewError_.clear();
    show_->setEnabled(true);
    if (!snapshot) {
        placementCanvas_->clearPlacement();
        previewSnapshot_.reset();
        if (!requestedVisible_) clearConfirmation();
        previewError_ = uiText("ui.gorev_cubugu_okunamadi") + QString::fromStdString(error);
        preview_->setText(previewError_);
        show_->setToolTip(previewError_);
        return;
    }
    if (!requestedVisible_ && approvedSnapshot_ && !sameGeometry(*approvedSnapshot_, *snapshot)) {
        clearConfirmation();
    }
    previewSnapshot_ = snapshot;
    auto candidate = request();
    candidate.regionConfirmed = true;
    // Auto-hide is a shell preference; the panel's visibility is independent.
    const auto result = calculatePlacement(snapshot->bounds, snapshot->dpi, snapshot->edge, false, candidate);
    if (!result.ok) {
        placementCanvas_->clearPlacement();
        if (!requestedVisible_) clearConfirmation();
        previewError_ = uiText("ui.bu_konum_kullanilamiyor") + QString::fromStdString(result.error);
        preview_->setText(previewError_);
        show_->setToolTip(previewError_);
        return;
    }
    const auto& rect = result.bounds;
    placementCanvas_->setPlacement(snapshot->bounds, rect);
    preview_->setText(uiText("ui.1_kenardan_2_px_3_px_genislik")
        .arg(side_->currentText()).arg(offset_->value()).arg(width_->value()));
    preview_->setToolTip(uiText("ui.secilen_bolge_x_1_y_2_genislik_3_yukseklik_4_piksel_olce")
        .arg(rect.left).arg(rect.top).arg(rect.width()).arg(rect.height())
        .arg(snapshot->dpi * 100.0 / 96.0)
        .arg(snapshot->autoHide ? uiText("ui.gorev_cubugu_otomatik_gizleniyor") : QString()));
    show_->setToolTip(confirmed_->isChecked() ? uiText("ui.paneli_secilen_bolgede_goster") :
        uiText("ui.once_bu_bolge_bos_kutusunu_isaretleyerek_konumu_onayla"));
}

void ApplicationController::clearConfirmation() {
    const QSignalBlocker blocker(confirmed_);
    confirmed_->setChecked(false);
    approvedSnapshot_.reset();
}

void ApplicationController::setStatus(const QString& text, bool error) {
    status_->setText(text);
    status_->setStyleSheet(error ? uiText("ui.color_a52b26") : QString());
}

void ApplicationController::updateReadinessStatus() {
    if (!previewError_.isEmpty()) setStatus(previewError_, true);
    else if (!confirmed_->isChecked())
        setStatus(uiText("ui.once_bu_bolge_bos_kutusunu_isaretle_ardindan_goster_e_ba"));
    else if (displayed_)
        setStatus(uiText("ui.panel_gosteriliyor_gizle_veya_cikis_ile_kapatabilirsin"));
    else setStatus(uiText("ui.konum_onaylandi_paneli_acmak_icin_goster_e_bas"));
}

void ApplicationController::invalidatePlacement() {
    clearConfirmation();
    refreshPreview();
    setStatus(previewError_.isEmpty() ?
        (requestedVisible_ ? uiText("ui.panel_eski_konumunda_gorunur_kaliyor_yeni_konum_icin_bol") :
                            uiText("ui.konum_degisti_bu_bolge_bos_kutusunu_yeniden_isaretle")) : previewError_,
        !previewError_.isEmpty());
}

void ApplicationController::showPanel() {
    const auto desired = preferences();
    if (requestedVisible_ && retainedBounds_ && desired.monitor == activePreferences_.monitor &&
        desired.side == activePreferences_.side && desired.widthDip == activePreferences_.widthDip &&
        desired.offsetDip == activePreferences_.offsetDip) {
        reevaluateVisibility();
        if (interactive_) controls_->hide();
        return;
    }
    const auto approved = approvedSnapshot_;
    refreshPreview();
    if (!previewError_.isEmpty()) {
        const QString reason = previewError_;
        setStatus(reason, true);
        return;
    }
    if (!approved || !previewSnapshot_ || !sameGeometry(*approved, *previewSnapshot_) ||
        !confirmed_->isChecked()) {
        setStatus(uiText("ui.paneli_acmak_icin_once_bu_bolge_bos_kutusunu_isaretleyer"), true);
        return;
    }
    const auto& snapshot = *previewSnapshot_;
    const auto result = calculatePlacement(snapshot.bounds, snapshot.dpi, snapshot.edge, false, request());
    if (!result.ok) {
        setStatus(QString::fromStdString(result.error), true);
        return;
    }
    const bool previouslyRequested = requestedVisible_;
    requestedVisible_ = true;
    QString error;
    const bool shown = services_.showPanel(result.bounds, error);
    if (!requestedVisible_) return;
    if (!shown) {
        requestedVisible_ = previouslyRequested;
        if (!requestedVisible_) safetyTimer_->stop();
        setStatus(uiText("ui.panel_acilamadi") + error, true);
        return;
    }
    retainedBounds_ = result.bounds;
    activePreferences_ = preferences();
    displayed_ = true;
    panel_.widgetHost()->setRunning(true);
    safetyTimer_->start();
    if (interactive_ && !panel_.watchTaskbar(reinterpret_cast<WId>(snapshot.hwnd), error)) {
        setStatus(uiText("ui.panel_gosteriliyor_anlik_siralama_takibi_kurulamadi") + error, true);
        controls_->hide();
        return;
    }
    setStatus(uiText("ui.panel_gosteriliyor_gizle_veya_cikis_ile_kapatabilirsin"));
    if (interactive_) controls_->hide();
}

void ApplicationController::reevaluateVisibility() {
    if (!requestedVisible_) {
        safetyTimer_->stop();
        return;
    }
    VisibilityInputs inputs;
    inputs.requested = true;
    inputs.approved = retainedBounds_.has_value();
    if (decideVisibility(inputs) != VisibilityDecision::Show) return;
    safetyTimer_->start();
    std::string probeError;
    const auto snapshot = services_.readTaskbarForMonitor(activePreferences_.monitor, probeError);
    Rect candidateBounds = *retainedBounds_;
    QString observerError;
    if (snapshot) {
        if (interactive_) {
            panel_.watchTaskbar(reinterpret_cast<WId>(snapshot->hwnd), observerError);
        }
        PlacementRequest active;
        active.side = activePreferences_.side;
        active.widthDip = activePreferences_.widthDip;
        active.offsetDip = activePreferences_.offsetDip;
        active.heightDip = 32;
        active.regionConfirmed = true;
        const auto placement = calculatePlacement(snapshot->bounds, snapshot->dpi, snapshot->edge, false, active);
        if (placement.ok) candidateBounds = placement.bounds;
    }
    QString error;
    if (!services_.retainPanel(candidateBounds, error)) {
        setStatus(uiText("ui.panelin_gorunurlugu_yenilenemedi") + error, true);
        return;
    }
    if (!requestedVisible_) return;
    retainedBounds_ = candidateBounds;
    displayed_ = true;
    if (!observerError.isEmpty()) {
        setStatus(uiText("ui.panel_gosteriliyor_anlik_siralama_takibi_yenilenemedi") + observerError, true);
        return;
    }
    setStatus(snapshot ? uiText("ui.panel_gosteriliyor_gizle_veya_cikis_ile_kapatabilirsin") :
        uiText("ui.panel_son_dogrulanan_konumunda_tutuluyor_gorev_cubugu_ok") +
            QString::fromStdString(probeError), !snapshot.has_value());
}

void ApplicationController::hideTemporarily() {
    panel_.hide();
    displayed_ = false;
}

void ApplicationController::hidePanel() {
    requestedVisible_ = false;
    if (safetyTimer_) safetyTimer_->stop();
    hideTemporarily();
    if (status_) setStatus(uiText("ui.panel_gizli"));
}

void ApplicationController::handleEnvironmentChange() {
    environmentTimer_->start();
}

void ApplicationController::handlePowerState(bool /*suspended*/) {
    environmentTimer_->start();
}

void ApplicationController::handleShellRecreation() {
    environmentTimer_->start();
}

void ApplicationController::activateControls() {
    if (!interactive_ || !controls_) return;
    showControls();
}

void ApplicationController::activatePanel() {
    if (!interactive_) return;
    showPanel();
}

void ApplicationController::showControls() {
    refreshPreview();
    controlPages_->setCurrentWidget(panelControlsPage_);
    layoutNavigation_->setChecked(true);
    if (!interactive_) return;
    controls_->setAttribute(Qt::WA_ShowWithoutActivating, false);
    controls_->showNormal();
    controls_->raise();
    controls_->activateWindow();
}

void ApplicationController::showWidgetSettings() {
    if (!widgetControls_) {
        widgetControls_ = std::make_unique<WidgetSettingsDialog>(panel_.widgetHost(), controls_.get());
        controlPages_->addWidget(widgetControls_.get());
        connect(widgetControls_.get(), &WidgetSettingsDialog::backRequested,
                this, &ApplicationController::showControls);
    }
    controlPages_->setCurrentWidget(widgetControls_.get());
    widgetNavigation_->setChecked(true);
    if (!interactive_) return;
    controls_->setAttribute(Qt::WA_ShowWithoutActivating, false);
    controls_->showNormal();
    widgetControls_->show();
    controls_->raise();
    controls_->activateWindow();
}

void ApplicationController::showDiagnostics() {
    std::string error;
    const auto snapshot = probeTaskbar(error);
    QString text = snapshot ? QString::fromStdString(windows::describe(*snapshot)) :
        uiText("ui.gorev_cubugu_okunamadi") + QString::fromStdString(error);
    if (settingsStore_) text += uiText("ui.ayar_dosyasi") + settingsStore_->filePath();
    QMessageBox::information(controls_.get(), uiText("ui.w11widget_tanilama"), text);
}
} // namespace w11widget
