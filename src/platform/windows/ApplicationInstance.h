#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <QObject>
#include <QString>
#include <functional>

namespace w11widget::windows {
// Coordinates cooperating v2 application versions within one profile/session.
// Settings and panel requests are delivered independently on the Qt thread.
class ApplicationInstance final : public QObject {
public:
    enum class StartResult { Primary, ActivatedExisting, Error };
    enum class Activation { Settings = 0, Panel = 1 };
    explicit ApplicationInstance(QObject* parent = nullptr);
    ~ApplicationInstance() override;
    StartResult start(const QString& scope, QString& error, bool activateExisting = true,
                      Activation action = Activation::Settings);
    void setActivationHandler(std::function<void()> handler);
    void setPanelActivationHandler(std::function<void()> handler);
private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void activate(Activation action);
    HANDLE mutex_ = nullptr;
    HWND window_ = nullptr;
    bool ownsMutex_ = false;
    bool pendingSettings_ = false;
    bool pendingPanel_ = false;
    std::function<void()> handler_;
    std::function<void()> panelHandler_;
};
} // namespace w11widget::windows
