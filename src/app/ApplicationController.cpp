#include "app/ApplicationController.h"
#include "core/VisibilityPolicy.h"
#include "core/WidgetHostModel.h"
#include "ui/WidgetSettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
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
            (result.missing ? QStringLiteral("Değişiklikler otomatik kaydedilecek.") :
                              QStringLiteral("Kayıtlı ayarlar yüklendi.")) :
            QStringLiteral("Ayar dosyası okunamadı; varsayılanlar kullanılıyor: ") + result.error;
    }

    safetyTimer_ = new QTimer(this);
    safetyTimer_->setObjectName(QStringLiteral("safetyWatchdog"));
    safetyTimer_->setInterval(1000);
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
        setStatus(QStringLiteral("Panel görünürlük koruması: ") + error, true);
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
    controls_->setWindowTitle(QStringLiteral("w11widget %1 — Panel kontrolü")
                                 .arg(QCoreApplication::applicationVersion()));
    controls_->setMinimumWidth(520);
    auto* rootLayout = new QVBoxLayout(controls_.get());
    controlPages_ = new QStackedWidget(controls_.get());
    controlPages_->setObjectName(QStringLiteral("controlPages"));
    panelControlsPage_ = new QWidget(controlPages_);
    panelControlsPage_->setObjectName(QStringLiteral("panelControlsPage"));
    controlPages_->addWidget(panelControlsPage_);
    rootLayout->addWidget(controlPages_);
    auto* layout = new QVBoxLayout(panelControlsPage_);
    auto* intro = new QLabel(QStringLiteral("1. Boş bölgeyi seç.  2. Gerekirse kutuyu yeniden işaretle.  3. Göster'e bas."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* form = new QFormLayout;

    monitor_ = new QComboBox;
    monitor_->setObjectName(QStringLiteral("panelMonitor"));
    monitor_->addItem(QStringLiteral("Birincil ekran"), QStringLiteral("primary"));
    side_ = new QComboBox;
    side_->setObjectName(QStringLiteral("panelSide"));
    side_->addItems({QStringLiteral("Sol"), QStringLiteral("Sağ")});
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
    opacity_ = new QSpinBox;
    opacity_->setObjectName(QStringLiteral("panelOpacity"));
    opacity_->setRange(25, 100);
    opacity_->setValue(95);
    opacity_->setSuffix(QStringLiteral("%"));
    form->addRow(QStringLiteral("&Ekran:"), monitor_);
    form->addRow(QStringLiteral("&Taraf:"), side_);
    form->addRow(QStringLiteral("&Genişlik:"), width_);
    form->addRow(QStringLiteral("&Kenardan uzaklık:"), offset_);
    form->addRow(QStringLiteral("&Opaklık:"), opacity_);
    layout->addLayout(form);

    auto* visibilityInfo = new QLabel(QStringLiteral("Panel, Göster'den sonra görünür kalır. Kapatmak için Gizle veya Çıkış'ı kullan."));
    visibilityInfo->setWordWrap(true);
    layout->addWidget(visibilityInfo);
    preview_ = new QLabel;
    preview_->setObjectName(QStringLiteral("placementPreview"));
    preview_->setWordWrap(true);
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(preview_);
    confirmed_ = new QCheckBox(QStringLiteral("Bu bölge boş; Başlat, uygulama simgeleri veya saati kapatmıyor."));
    confirmed_->setObjectName(QStringLiteral("confirmRegion"));
    layout->addWidget(confirmed_);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("panelStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    settingsStatus_ = new QLabel;
    settingsStatus_->setObjectName(QStringLiteral("settingsStatus"));
    settingsStatus_->setWordWrap(true);
    layout->addWidget(settingsStatus_);

    auto* widgets = new QPushButton(QStringLiteral("&Widget'ları düzenle…"));
    widgets->setObjectName(QStringLiteral("manageWidgets"));
    layout->addWidget(widgets);
    connect(widgets, &QPushButton::clicked, this, &ApplicationController::showWidgetSettings);

    auto* buttons = new QHBoxLayout;
    show_ = new QPushButton(QStringLiteral("&Göster"));
    show_->setObjectName(QStringLiteral("showPanel"));
    show_->setDefault(true);
    auto* hide = new QPushButton(QStringLiteral("G&izle"));
    hide->setObjectName(QStringLiteral("hidePanel"));
    auto* diagnostics = new QPushButton(QStringLiteral("&Tanılama"));
    auto* reset = new QPushButton(QStringLiteral("&Sıfırla"));
    reset->setObjectName(QStringLiteral("resetPreferences"));
    auto* exit = new QPushButton(QStringLiteral("&Çıkış"));
    for (auto* button : {show_, hide, diagnostics, reset, exit}) buttons->addWidget(button);
    layout->addLayout(buttons);
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
}

void ApplicationController::populateMonitors() {
    const QString selected = monitor_->currentData().toString();
    const QSignalBlocker blocker(monitor_);
    monitor_->clear();
    monitor_->addItem(QStringLiteral("Birincil ekran"), QStringLiteral("primary"));
    std::string error;
    for (const auto& monitor : services_.monitors(error)) {
        const QString device = QString::fromStdWString(monitor.device);
        const QString label = QStringLiteral("%1 — %2×%3%4").arg(device)
            .arg(monitor.bounds.width()).arg(monitor.bounds.height())
            .arg(monitor.primary ? QStringLiteral(" (birincil)") : QString());
        monitor_->addItem(label, device);
    }
    int index = monitor_->findData(selected);
    if (index < 0 && !selected.isEmpty()) {
        monitor_->addItem(selected + QStringLiteral(" (bağlı değil)"), selected);
        index = monitor_->count() - 1;
    }
    monitor_->setCurrentIndex(index < 0 ? 0 : index);
}

void ApplicationController::applyPreferences(const PanelSettings& settings) {
    applyingPreferences_ = true;
    populateMonitors();
    int index = monitor_->findData(settings.monitor);
    if (index < 0) {
        monitor_->addItem(settings.monitor + QStringLiteral(" (bağlı değil)"), settings.monitor);
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
        setStatus(QStringLiteral("Widget ayarları uygulanamadı: ") + widgetError, true);
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
    settingsStatus_->setText(saved ? QStringLiteral("Ayarlar kaydedildi.") :
        QStringLiteral("Ayarlar kaydedilemedi: ") + error);
    settingsStatus_->setToolTip(settingsStore_->filePath());
}

void ApplicationController::resetPreferences() {
    hidePanel();
    clearConfirmation();
    applyPreferences(PanelSettings{});
    refreshPreview();
    preferencesDirty_ = true;
    savePreferences();
    setStatus(QStringLiteral("Varsayılan ayarlara dönüldü. Bölgeyi yeniden onayla."));
}

void ApplicationController::connectScreen(QScreen* screen) {
    connect(screen, &QScreen::geometryChanged, this, [this] { handleEnvironmentChange(); });
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { handleEnvironmentChange(); });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { handleEnvironmentChange(); });
}

void ApplicationController::createTray() {
    if (!services_.trayAvailable()) return;
    trayMenu_ = std::make_unique<QMenu>();
    trayMenu_->addAction(QStringLiteral("Paneli göster"), this, &ApplicationController::showPanel);
    trayMenu_->addAction(QStringLiteral("Paneli gizle"), this, &ApplicationController::hidePanel);
    trayMenu_->addAction(QStringLiteral("Ayarlar…"), this, &ApplicationController::showControls);
    trayMenu_->addAction(QStringLiteral("Widget'ları düzenle…"), this, &ApplicationController::showWidgetSettings);
    trayMenu_->addAction(QStringLiteral("Tanılama…"), this, &ApplicationController::showDiagnostics);
    trayMenu_->addSeparator();
    trayMenu_->addAction(QStringLiteral("Çıkış"), qApp, &QCoreApplication::quit);
    tray_ = std::make_unique<QSystemTrayIcon>(appIcon());
    tray_->setToolTip(QStringLiteral("w11widget — Panel kontrolü"));
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
        previewSnapshot_.reset();
        if (!requestedVisible_) clearConfirmation();
        previewError_ = QStringLiteral("Görev çubuğu okunamadı: ") + QString::fromStdString(error);
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
        if (!requestedVisible_) clearConfirmation();
        previewError_ = QStringLiteral("Bu konum kullanılamıyor: ") + QString::fromStdString(result.error);
        preview_->setText(previewError_);
        show_->setToolTip(previewError_);
        return;
    }
    const auto& rect = result.bounds;
    preview_->setText(QStringLiteral("Seçilen bölge: x=%1, y=%2, genişlik=%3, yükseklik=%4 piksel\nÖlçek: %5%%6")
        .arg(rect.left).arg(rect.top).arg(rect.width()).arg(rect.height())
        .arg(snapshot->dpi * 100.0 / 96.0)
        .arg(snapshot->autoHide ? QStringLiteral(" — görev çubuğu otomatik gizleniyor") : QString()));
    show_->setToolTip(confirmed_->isChecked() ? QStringLiteral("Paneli seçilen bölgede göster.") :
        QStringLiteral("Önce 'Bu bölge boş' kutusunu işaretleyerek konumu onayla."));
}

void ApplicationController::clearConfirmation() {
    const QSignalBlocker blocker(confirmed_);
    confirmed_->setChecked(false);
    approvedSnapshot_.reset();
}

void ApplicationController::setStatus(const QString& text, bool error) {
    status_->setText(text);
    status_->setStyleSheet(error ? QStringLiteral("color: #a52b26;") : QString());
}

void ApplicationController::updateReadinessStatus() {
    if (!previewError_.isEmpty()) setStatus(previewError_, true);
    else if (!confirmed_->isChecked())
        setStatus(QStringLiteral("Önce 'Bu bölge boş' kutusunu işaretle; ardından Göster'e bas."));
    else if (displayed_)
        setStatus(QStringLiteral("Panel gösteriliyor. Gizle veya Çıkış ile kapatabilirsin."));
    else setStatus(QStringLiteral("Konum onaylandı. Paneli açmak için Göster'e bas."));
}

void ApplicationController::invalidatePlacement() {
    clearConfirmation();
    refreshPreview();
    setStatus(previewError_.isEmpty() ?
        (requestedVisible_ ? QStringLiteral("Panel eski konumunda görünür kalıyor. Yeni konum için bölgeyi onaylayıp Göster'e bas.") :
                            QStringLiteral("Konum değişti. 'Bu bölge boş' kutusunu yeniden işaretle.")) : previewError_,
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
        setStatus(QStringLiteral("Paneli açmak için önce 'Bu bölge boş' kutusunu işaretleyerek konumu onayla."), true);
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
        setStatus(QStringLiteral("Panel açılamadı: ") + error, true);
        return;
    }
    retainedBounds_ = result.bounds;
    activePreferences_ = preferences();
    displayed_ = true;
    panel_.widgetHost()->setRunning(true);
    safetyTimer_->start();
    if (interactive_ && !panel_.watchTaskbar(reinterpret_cast<WId>(snapshot.hwnd), error)) {
        setStatus(QStringLiteral("Panel gösteriliyor; anlık sıralama takibi kurulamadı: ") + error, true);
        controls_->hide();
        return;
    }
    setStatus(QStringLiteral("Panel gösteriliyor. Gizle veya Çıkış ile kapatabilirsin."));
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
        setStatus(QStringLiteral("Panelin görünürlüğü yenilenemedi: ") + error, true);
        return;
    }
    if (!requestedVisible_) return;
    retainedBounds_ = candidateBounds;
    displayed_ = true;
    if (!observerError.isEmpty()) {
        setStatus(QStringLiteral("Panel gösteriliyor; anlık sıralama takibi yenilenemedi: ") + observerError, true);
        return;
    }
    setStatus(snapshot ? QStringLiteral("Panel gösteriliyor. Gizle veya Çıkış ile kapatabilirsin.") :
        QStringLiteral("Panel son doğrulanan konumunda tutuluyor. Görev çubuğu okunamadı: ") +
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
    if (status_) setStatus(QStringLiteral("Panel gizli."));
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
        QStringLiteral("Görev çubuğu okunamadı: ") + QString::fromStdString(error);
    if (settingsStore_) text += QStringLiteral("\n\nAyar dosyası: ") + settingsStore_->filePath();
    QMessageBox::information(controls_.get(), QStringLiteral("w11widget — Tanılama"), text);
}
} // namespace w11widget
