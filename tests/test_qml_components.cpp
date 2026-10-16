#include "core/WidgetHostModel.h"
#include "ui/ScreenGeometry.h"
#include "ui/Strings.h"
#include "ui/WidgetSettingsDialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlListReference>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickStyle>
#include <QScreen>
#include <QTimer>

#include <iostream>
#include <memory>
#include <limits>
#include <vector>

#ifdef NDEBUG
#error Release tests must keep assertions enabled.
#endif

namespace {

int enumValue(const QObject* object, const char* propertyName, const char* key)
{
    const int index = object->metaObject()->indexOfProperty(propertyName);
    if (index < 0) return -1;
    return object->metaObject()->property(index).enumerator().keyToValue(key);
}

bool checkFlyout(QObject* popup)
{
    const int window = enumValue(popup, "popupType", "Window");
    const int escape = enumValue(popup, "closePolicy", "CloseOnEscape");
    const int outside = enumValue(popup, "closePolicy", "CloseOnPressOutside");
    const int policy = popup->property("closePolicy").toInt();
    const bool valid = window >= 0 && escape > 0 && outside > 0
        && popup->property("popupType").toInt() == window
        && (policy & escape) != 0 && (policy & outside) != 0
        && popup->property("focus").toBool() && !popup->property("modal").toBool();
    if (!valid) {
        std::cerr << popup->objectName().toStdString() << ": popupType="
                  << popup->property("popupType").toInt() << " expected=" << window
                  << " closePolicy=" << policy << " escape=" << escape << " outside=" << outside
                  << " focus=" << popup->property("focus").toBool()
                  << " modal=" << popup->property("modal").toBool() << '\n';
    }
    return valid;
}

void settleRendering()
{
    QEventLoop loop;
    QTimer::singleShot(150, &loop, &QEventLoop::quit);
    loop.exec();
}

bool capture(QQuickView& view, w11widget::WidgetHostModel& host,
             const std::vector<std::unique_ptr<QObject>>& items, const QString& directory)
{
    QDir output(directory);
    if (!output.mkpath(QStringLiteral("."))) return false;
    w11widget::WidgetSettingsDialog settings(&host);
    settings.resize(900, 600);
    settings.show();
    settleRendering();
    if (!settings.grab().save(output.filePath(QStringLiteral("widget-settings-after.png")))) return false;
    settings.hide();

    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(640, 32);
    view.setPosition(80, 720);
    view.show();
    settleRendering();
    if (!view.grabWindow().save(output.filePath(QStringLiteral("panel-after.png")))) return false;
    for (const auto& object : items) {
        auto* item = qobject_cast<QQuickItem*>(object.get());
        if (!item) return false;
        for (auto* popup : item->findChildren<QObject*>()) {
            if (!popup->objectName().endsWith(QStringLiteral("Flyout"))
                || popup->metaObject()->indexOfProperty("popupType") < 0) continue;
            item->setParentItem(view.rootObject());
            item->setWidth(item->implicitWidth());
            item->setHeight(26);
            if (item->metaObject()->indexOfMethod("resetEditor()") >= 0)
                QMetaObject::invokeMethod(item, "resetEditor");
            if (!QMetaObject::invokeMethod(popup, "openFor", Q_ARG(QVariant, QVariant::fromValue(item)))) return false;
            settleRendering();
            auto* content = qobject_cast<QQuickItem*>(popup->property("contentItem").value<QObject*>());
            auto* window = content ? content->window() : nullptr;
            if (!window || window == &view || !window->isVisible()
                || !window->grabWindow().save(output.filePath(popup->objectName() + QStringLiteral("-after.png")))) return false;
            const auto workArea = popup->property("workArea").toRect();
            if (!workArea.isValid() || !workArea.contains(window->geometry())) {
                std::cerr << "FAIL: flyout window exceeds the available screen work area\n";
                return false;
            }
            QQuickItem oversizedBody;
            oversizedBody.setWidth(1);
            oversizedBody.setImplicitHeight(workArea.height() * 2);
            oversizedBody.setHeight(oversizedBody.implicitHeight());
            QQmlListReference body(popup, "body");
            if (!body.append(&oversizedBody)) return false;
            settleRendering();
            bool scrollable = false;
            for (auto* child : content->findChildren<QQuickItem*>()) {
                if (child->property("interactive").toBool()
                    && child->property("contentHeight").toReal() > child->height() + 1
                    && child->setProperty("contentY", child->property("contentHeight").toReal() - child->height())) {
                    scrollable = child->property("contentY").toReal() > 0;
                    break;
                }
            }
            if (!workArea.contains(window->geometry()) || !scrollable) {
                std::cerr << "FAIL: oversized flyout must stay within the work area and scroll\n";
                std::cerr << "popup=" << popup->objectName().toStdString()
                          << " geometry=" << window->x() << ',' << window->y() << ',' << window->width() << ',' << window->height()
                          << " workArea=" << workArea.x() << ',' << workArea.y() << ',' << workArea.width() << ',' << workArea.height()
                          << " scrollable=" << scrollable << '\n';
                for (auto* child : content->findChildren<QQuickItem*>()) {
                    if (child->property("contentHeight").isValid())
                        std::cerr << child->metaObject()->className() << " height=" << child->height()
                                  << " contentHeight=" << child->property("contentHeight").toReal()
                                  << " interactive=" << child->property("interactive").toBool() << '\n';
                }
                return false;
            }
            view.hide();
            settleRendering();
            if (popup->property("visible").toBool() || window->isVisible()) {
                std::cerr << "FAIL: hiding the owner window must close its flyout\n";
                return false;
            }
            item->setParentItem(nullptr);
            view.show();
        }
    }
    view.hide();
    std::cout << "Captured settings, panel and flyouts: " << output.absolutePath().toStdString()
              << "; oversized scrolling, screen bounds and owner-hide dismissal PASS\n";
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption captureDirectory(QStringLiteral("capture-dir"),
        QStringLiteral("Show portable UI and save screenshots (does not exercise Windows integration)."),
        QStringLiteral("path"));
    parser.addOption(captureDirectory);
    parser.process(app);
    w11widget::WidgetHostModel host;
    w11widget::WidgetConfigurations configuration;
    for (const auto& descriptor : host.registry().descriptors()) {
        configuration.push_back({QStringLiteral("smoke-") + descriptor.typeId,
                                 descriptor.typeId, true, true, descriptor.defaultWidthDip,
                                 descriptor.defaultSettings});
    }
    QString error;
    if (configuration.empty() || !host.setConfiguration(configuration, error)) {
        std::cerr << "FAIL: built-in widget configuration: " << error.toStdString() << '\n';
        return 1;
    }
    host.setRunning(true);

    QQuickView view;
    bool qmlWarning = false;
    QObject::connect(view.engine(), &QQmlEngine::warnings, [&qmlWarning](const QList<QQmlError>& warnings) {
        qmlWarning = true;
        for (const auto& warning : warnings) std::cerr << warning.toString().toStdString() << '\n';
    });
    view.rootContext()->setContextProperty(QStringLiteral("widgetHostModel"), &host);
    view.rootContext()->setContextProperty(QStringLiteral("I18n"), new w11widget::Strings(&view));
    auto* screens = new w11widget::ScreenGeometry(&view);
    view.rootContext()->setContextProperty(QStringLiteral("Screens"), screens);
    if (auto* primary = QGuiApplication::primaryScreen()) {
        const auto center = primary->geometry().center();
        const auto* screenAtCenter = QGuiApplication::screenAt(center);
        const auto expected = screenAtCenter ? screenAtCenter->availableGeometry() : primary->availableGeometry();
        if (screens->availableAt(center.x(), center.y()) != expected
            || screens->availableAt(std::numeric_limits<int>::max(), std::numeric_limits<int>::max())
                != primary->availableGeometry()) {
            std::cerr << "FAIL: screen work area selection or primary-screen fallback\n";
            return 1;
        }
    } else if (screens->availableAt(0, 0) != QRect(0, 0, 800, 600)) {
        std::cerr << "FAIL: bounded headless screen fallback\n";
        return 1;
    }
    view.setSource(QUrl(QStringLiteral("qrc:/qml/MainPanel.qml")));
    if (view.status() != QQuickView::Ready || !view.rootObject()) {
        for (const auto& detail : view.errors()) std::cerr << detail.toString().toStdString() << '\n';
        std::cerr << "FAIL: main panel QML did not load\n";
        return 1;
    }

    std::vector<std::unique_ptr<QObject>> items;
    int flyoutCount = 0;
    for (int row = 0; row < host.rowCount(); ++row) {
        const auto index = host.index(row, 0);
        const auto moduleError = host.data(index, w11widget::WidgetHostModel::ErrorMessageRole).toString();
        if (!moduleError.isEmpty()) {
            std::cerr << "FAIL: module initialization: " << moduleError.toStdString() << '\n';
            return 1;
        }
        QQmlComponent component(view.engine(),
            QUrl(host.data(index, w11widget::WidgetHostModel::ComponentSourceRole).toString()));
        const QVariantMap properties{
            {QStringLiteral("widgetModel"), host.data(index, w11widget::WidgetHostModel::ViewModelRole)},
            {QStringLiteral("widgetInstanceId"), configuration[static_cast<std::size_t>(row)].instanceId}
        };
        std::unique_ptr<QObject> item(component.createWithInitialProperties(properties, view.rootContext()));
        if (!item || component.isError()) {
            for (const auto& detail : component.errors()) std::cerr << detail.toString().toStdString() << '\n';
            std::cerr << "FAIL: built-in module QML did not load\n";
            return 1;
        }
        for (auto* popup : item->findChildren<QObject*>()) {
            if (!popup->objectName().endsWith(QStringLiteral("Flyout"))
                || popup->metaObject()->indexOfProperty("popupType") < 0) continue;
            if (!checkFlyout(popup)) {
                std::cerr << "FAIL: flyout must use a window, focus, Escape and outside-click dismissal\n";
                return 1;
            }
            ++flyoutCount;
        }
        items.push_back(std::move(item));
    }
    app.processEvents();
    if (qmlWarning || view.isVisible() || flyoutCount != 3) {
        std::cerr << "FAIL: QML warning, unexpected visible panel or missing flyout\n";
        return 1;
    }
    if (parser.isSet(captureDirectory)
        && (!capture(view, host, items, parser.value(captureDirectory)) || qmlWarning)) {
        std::cerr << "FAIL: portable UI screenshot capture\n";
        return 1;
    }
    std::cout << "PASS: main panel and " << host.rowCount()
              << " built-in QML modules loaded without warnings; " << flyoutCount
              << " window flyout policies checked; no native Windows behavior tested\n";
    return 0;
}
