#include "PanelWindow.h"

#include "platform/windows/WindowBehavior.h"
#include "core/WidgetHostModel.h"

#include <QColor>
#include <QKeyEvent>
#include <QQmlError>
#include <QQmlContext>
#include <QQuickItem>
#include <QStringList>
#include <QSurfaceFormat>
#include <QUrl>

namespace w11widget {

PanelWindow::PanelWindow(QWindow* parent)
    : QQuickView(parent)
    , m_zOrderObserver(std::make_unique<windows::TaskbarZOrderObserver>(
          [this] { retainNativeStacking(); }))
{
    // This is an independent window, even if a caller supplied a parent.
    // Native TOOLWINDOW styling suppresses its icon without Qt tool lifecycle.
    setParent(nullptr);
    setTransientParent(nullptr);
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
             | Qt::WindowDoesNotAcceptFocus);
    QSurfaceFormat surfaceFormat = requestedFormat();
    surfaceFormat.setAlphaBufferSize(8);
    setFormat(surfaceFormat);
    setColor(QColor(Qt::transparent));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setTitle(QStringLiteral("w11widget"));
    m_widgetHost = new WidgetHostModel(this);
    rootContext()->setContextProperty(QStringLiteral("widgetHostModel"), m_widgetHost);
}

bool PanelWindow::initialize(QString& error)
{
    error.clear();
    if (m_initialized) {
        return true;
    }
    setSource(QUrl(QStringLiteral("qrc:/qml/MainPanel.qml")));
    if (status() != QQuickView::Ready || rootObject() == nullptr) {
        QStringList details;
        for (const QQmlError& qmlError : errors()) {
            details.append(qmlError.toString());
        }
        error = QStringLiteral("The QML sample panel could not be loaded.");
        if (!details.isEmpty()) {
            error += QLatin1Char('\n') + details.join(QLatin1Char('\n'));
        }
        hide();
        return false;
    }
    // winId() creates this application's window without showing it.
    if (!windows::preparePanelWindow(winId(), error) || !ensureCompositionProtection(error)) {
        hide();
        return false;
    }
    if (!QObject::connect(rootObject(), SIGNAL(settingsRequested()), this, SIGNAL(settingsRequested()))) {
        error = QStringLiteral("The QML panel settings action could not be connected.");
        hide();
        return false;
    }
    m_initialized = true;
    return true;
}

bool PanelWindow::ensureCompositionProtection(QString& error)
{
    const WId window = winId();
    if (m_compositionWindow == window) return true;
    if (!windows::protectPanelComposition(window, error)) return false;
    m_compositionWindow = window;
    return true;
}

PanelWindow::~PanelWindow()
{
    m_visibilityPinned = false;
    m_zOrderObserver->stop();
    m_widgetHost->setRunning(false);
}

void PanelWindow::hide()
{
    m_visibilityPinned = false;
    m_zOrderObserver->stop();
    m_widgetHost->setRunning(false);
    QQuickView::hide();
}

bool PanelWindow::watchTaskbar(WId taskbarWindow, QString& error)
{
    m_taskbarWindow = taskbarWindow;
    if (!m_visibilityPinned) {
        error.clear();
        return true;
    }
    if (!m_zOrderObserver->observe(m_taskbarWindow, error)) return false;
    // Close the gap between initial placement and installing/rebinding events.
    retainNativeStacking();
    return true;
}

void PanelWindow::retainNativeStacking()
{
    if (!m_visibilityPinned || m_retainingStacking) return;
    m_retainingStacking = true;
    QString error;
    if (!windows::placePanelWindow(winId(), m_requestedBounds, true, m_actualBounds, error))
        emit visibilityMaintenanceFailed(error);
    m_retainingStacking = false;
}

bool PanelWindow::showAt(const Rect& physicalBounds, QString& error)
{
    return keepVisibleAt(physicalBounds, error);
}

bool PanelWindow::keepVisibleAt(const Rect& physicalBounds, QString& error)
{
    error.clear();
    if (!m_initialized) {
        error = QStringLiteral("Initialize the QML panel before showing it.");
        return false;
    }
    if (!windows::preparePanelWindow(winId(), error) || !ensureCompositionProtection(error)) return false;

    // QtQuick needs Qt's visibility state as well as a visible native HWND.
    // Initial placement is hidden; existing visible windows stay visible.
    if (!isVisible() || visibility() == QWindow::Minimized) {
        if (!windows::placePanelWindow(winId(), physicalBounds, false,
                                       m_actualBounds, error)) return false;
        showNormal();
        if (!windows::preparePanelWindow(winId(), error)) return false;
    }
    if (!windows::placePanelWindow(winId(), physicalBounds, true,
                                   m_actualBounds, error)) return false;
    m_requestedBounds = physicalBounds;
    m_visibilityPinned = true;
    m_widgetHost->setRunning(true);
    if (m_taskbarWindow != 0) {
        QString observerError;
        if (!m_zOrderObserver->observe(m_taskbarWindow, observerError))
            emit visibilityMaintenanceFailed(observerError);
    }
    return true;
}

bool PanelWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        const windows::PanelNativeMessage native =
            windows::inspectPanelNativeMessage(message, m_visibilityPinned);
        if (native.compositionChanged && m_initialized) {
            m_compositionWindow = 0;
            QString error;
            if (!ensureCompositionProtection(error)) emit visibilityMaintenanceFailed(error);
        }
        if (native.stackingChanged) retainNativeStacking();
        if (native.clockChanged || native.powerTransition == 2) {
            m_widgetHost->refreshViewModels();
        }
        if (native.placementInvalidated) {
            if (native.powerTransition != 0) {
                emit powerStateChanged(native.powerTransition == 1);
            }
            if (native.shellRecreated) {
                emit shellRecreated();
            }
            emit environmentChanged();
        }
        if (native.handled) {
            if (result != nullptr) {
                *result = native.result;
            }
            return true;
        }
    }
    return QQuickView::nativeEvent(eventType, message, result);
}

void PanelWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        emit hideRequested();
        return;
    }
    QQuickView::keyPressEvent(event);
}

} // namespace w11widget
