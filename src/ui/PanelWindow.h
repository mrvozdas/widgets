#pragma once

#include "core/PanelPositioner.h"

#include <QQuickView>
#include <memory>

class QKeyEvent;

namespace w11widget {
namespace windows { class TaskbarZOrderObserver; }
class WidgetHostModel;

class PanelWindow final : public QQuickView {
    Q_OBJECT

public:
    explicit PanelWindow(QWindow* parent = nullptr);
    ~PanelWindow() override;

    bool initialize(QString& error);
    bool showAt(const Rect& physicalBounds, QString& error);
    // Restore our visible/topmost position without hide/show cycles or focus.
    bool keepVisibleAt(const Rect& physicalBounds, QString& error);
    bool watchTaskbar(WId taskbarWindow, QString& error);
    [[nodiscard]] WidgetHostModel* widgetHost() const { return m_widgetHost; }

public slots:
    // Explicit user Hide/Exit disables the native visibility protection first.
    void hide();

signals:
    void environmentChanged();
    void powerStateChanged(bool suspended);
    void shellRecreated();
    void hideRequested();
    void settingsRequested();
    void visibilityMaintenanceFailed(const QString& error);

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool ensureCompositionProtection(QString& error);
    void retainNativeStacking();
    bool m_initialized = false;
    bool m_visibilityPinned = false;
    bool m_retainingStacking = false;
    WId m_taskbarWindow = 0;
    WId m_compositionWindow = 0;
    WidgetHostModel* m_widgetHost = nullptr;
    std::unique_ptr<windows::TaskbarZOrderObserver> m_zOrderObserver;
    Rect m_requestedBounds{};
    Rect m_actualBounds{};
};

} // namespace w11widget
