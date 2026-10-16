#include "app/ApplicationController.h"
#include "core/WidgetHostModel.h"
#include "platform/windows/ApplicationInstance.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QQuickItem>
#include <QQuickStyle>
#include <QScreen>
#include <QTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    QQuickWindow::setDefaultAlphaBuffer(true);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("w11widget"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(W11WIDGET_APP_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows görev çubuğu için örnek panel"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption smoke(QStringLiteral("smoke-test"), QStringLiteral("Load QML without showing windows or creating a tray icon."));
    parser.addOption(smoke);
    QCommandLineOption benchmark(QStringLiteral("benchmark"),
        QStringLiteral("Show an isolated floating reference panel without reading user settings."));
    QCommandLineOption benchmarkHidden(QStringLiteral("benchmark-hidden"),
        QStringLiteral("Use the panel's normal hide lifecycle during an isolated benchmark."));
    QCommandLineOption duration(QStringLiteral("duration-seconds"),
        QStringLiteral("Exit the benchmark after 10-3600 seconds."), QStringLiteral("seconds"), QStringLiteral("60"));
    parser.addOption(benchmark);
    parser.addOption(benchmarkHidden);
    parser.addOption(duration);
    QCommandLineOption background(QStringLiteral("background"), QStringLiteral("Start in the tray without opening settings."));
    parser.addOption(background);
    QCommandLineOption settings(QStringLiteral("settings"), QStringLiteral("Open panel settings."));
    parser.addOption(settings);
    parser.process(app);
    if (parser.isSet(benchmarkHidden) && !parser.isSet(benchmark)) {
        std::cerr << "--benchmark-hidden requires --benchmark.\n";
        return 2;
    }
    if (parser.isSet(background) && parser.isSet(settings)) {
        parser.showHelp(1);
    }
    if (parser.isSet(benchmark)) {
        bool validDuration = false;
        const int seconds = parser.value(duration).toInt(&validDuration);
        if (!validDuration || seconds < 10 || seconds > 3600 || parser.isSet(background) || parser.isSet(settings)) {
            std::cerr << "Invalid benchmark options.\n";
            return 2;
        }
        w11widget::PanelWindow panel;
        QString error;
        if (!panel.initialize(error)) {
            std::cerr << error.toStdString() << '\n';
            return 1;
        }
        const auto* screen = QGuiApplication::primaryScreen();
        if (!screen) return 1;
        const auto area = screen->availableGeometry();
        const qreal scale = screen->devicePixelRatio();
        const int left = qRound((area.x() + 80) * scale);
        const int top = qRound((area.y() + 120) * scale);
        if (!panel.showAt({left, top, left + qRound(520 * scale), top + qRound(32 * scale)}, error)) {
            std::cerr << error.toStdString() << '\n';
            return 1;
        }
        // Render an initial frame before exercising the real hidden lifecycle.
        if (parser.isSet(benchmarkHidden)) QTimer::singleShot(1000, &panel, &w11widget::PanelWindow::hide);
        QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (parser.isSet(smoke)) {
        w11widget::PanelWindow panel;
        QString error;
        if (!panel.initialize(error) || !panel.rootObject() || panel.isVisible()) {
            std::cerr << "QML SMOKE TEST: FAIL " << error.toStdString() << '\n';
            return 1;
        }
        auto* host = panel.widgetHost();
        w11widget::WidgetConfigurations configuration;
        for (const auto& descriptor : host->registry().descriptors()) {
            configuration.push_back({QStringLiteral("smoke-") + descriptor.typeId,
                                     descriptor.typeId, true, true, descriptor.defaultWidthDip,
                                     descriptor.defaultSettings});
        }
        if (!host->setConfiguration(configuration, error)) {
            std::cerr << "QML SMOKE TEST: FAIL " << error.toStdString() << '\n';
            return 1;
        }
        // Load every built-in component, including modules that import Controls.
        // Default sample-only startup cannot detect missing deployed QML DLLs.
        for (int row = 0; row < host->rowCount(); ++row) {
            const auto index = host->index(row, 0);
            if (!host->data(index, w11widget::WidgetHostModel::ErrorMessageRole).toString().isEmpty()) {
                std::cerr << "QML SMOKE TEST: FAIL module initialization\n";
                return 1;
            }
            QQmlComponent component(panel.engine(),
                QUrl(host->data(index, w11widget::WidgetHostModel::ComponentSourceRole).toString()));
            const QVariantMap properties{
                {QStringLiteral("widgetModel"), host->data(index, w11widget::WidgetHostModel::ViewModelRole)},
                {QStringLiteral("widgetInstanceId"), configuration[static_cast<std::size_t>(row)].instanceId}
            };
            std::unique_ptr<QObject> item(component.createWithInitialProperties(properties, panel.rootContext()));
            if (!item || component.isError()) {
                for (const auto& detail : component.errors()) std::cerr << detail.toString().toStdString() << '\n';
                std::cerr << "QML SMOKE TEST: FAIL module component\n";
                return 1;
            }
        }
        if (panel.isVisible()) return 1;
        std::cout << "QML SMOKE TEST: PASS (all built-in modules loaded; overlay remains hidden)\n";
        return 0;
    }
    QString error;
    w11widget::windows::ApplicationInstance instance;
    // Help/version/smoke return above, without touching the user's live instance.
    const auto launch = instance.start(w11widget::SettingsStore::defaultPath().toCaseFolded(), error,
        parser.isSet(settings), parser.isSet(settings)
            ? w11widget::windows::ApplicationInstance::Activation::Settings
            : w11widget::windows::ApplicationInstance::Activation::Panel);
    if (launch == w11widget::windows::ApplicationInstance::StartResult::ActivatedExisting) return 0;
    if (launch == w11widget::windows::ApplicationInstance::StartResult::Error) {
        QMessageBox::critical(nullptr, QStringLiteral("w11widget — Açılış hatası"), error);
        return 1;
    }
    w11widget::ApplicationController controller;
    const auto mode = parser.isSet(settings) ? w11widget::ApplicationController::StartupMode::Settings
        : parser.isSet(background) ? w11widget::ApplicationController::StartupMode::Background
                                  : w11widget::ApplicationController::StartupMode::Panel;
    if (!controller.start(error, true, mode)) {
        std::cerr << "STARTUP ERROR: " << error.toStdString() << '\n';
        QMessageBox::critical(nullptr, QStringLiteral("w11widget — Başlatma hatası"), error);
        return 1;
    }
    instance.setActivationHandler([&controller] { controller.activateControls(); });
    instance.setPanelActivationHandler([&controller] { controller.activatePanel(); });
    const int result = app.exec();
    instance.setActivationHandler({});
    instance.setPanelActivationHandler({});
    return result;
}
