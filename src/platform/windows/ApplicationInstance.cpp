#include "platform/windows/ApplicationInstance.h"
#include <QCryptographicHash>
#include <QMetaObject>
#include <utility>

namespace w11widget::windows {
namespace {
constexpr wchar_t windowClass[] = L"w11widget.ApplicationInstance.v2";
constexpr UINT activateMessage = WM_APP + 0x251;
constexpr LRESULT activationAccepted = 0x573131;
QString nativeError(const wchar_t* operation) {
    return QString::fromWCharArray(operation) + QStringLiteral(" (Windows %1)").arg(GetLastError());
}
}

ApplicationInstance::ApplicationInstance(QObject* parent) : QObject(parent) {}
ApplicationInstance::~ApplicationInstance() {
    if (window_) DestroyWindow(window_);
    if (ownsMutex_) ReleaseMutex(mutex_);
    if (mutex_) CloseHandle(mutex_);
}

ApplicationInstance::StartResult ApplicationInstance::start(const QString& scope, QString& error,
                                                           bool activateExisting, Activation action) {
    if (scope.isEmpty() || mutex_
        || (action != Activation::Settings && action != Activation::Panel)) {
        error = QStringLiteral("Geçersiz uygulama oturumu.");
        return StartResult::Error;
    }
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        error = nativeError(L"ProcessIdToSessionId");
        return StartResult::Error;
    }
    const auto hash = QCryptographicHash::hash(scope.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto key = (QStringLiteral("w11widget-app-v2-%1-").arg(session)
                      + QString::fromLatin1(hash)).toStdWString();
    const auto mutexName = L"Local\\" + key;
    mutex_ = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex_) { error = nativeError(L"CreateMutex"); return StartResult::Error; }
    const DWORD wait = WaitForSingleObject(mutex_, 0);
    if (wait == WAIT_TIMEOUT) {
        if (!activateExisting) return StartResult::ActivatedExisting;
        const ULONGLONG deadline = GetTickCount64() + 5000;
        do {
            const HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, windowClass, key.c_str());
            if (existing) {
                DWORD primaryPid = 0;
                GetWindowThreadProcessId(existing, &primaryPid);
                if (primaryPid) AllowSetForegroundWindow(primaryPid);
                DWORD_PTR response = 0;
                SetLastError(ERROR_SUCCESS);
                if (SendMessageTimeoutW(existing, activateMessage, static_cast<WPARAM>(action), 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &response)
                    && response == static_cast<DWORD_PTR>(activationAccepted)) {
                    return StartResult::ActivatedExisting;
                }
                if (GetLastError() == ERROR_SUCCESS) SetLastError(ERROR_TIMEOUT);
                error = nativeError(L"Açık uygulama yanıt vermedi");
                return StartResult::Error;
            }
            // Initialization owns the mutex before publishing its endpoint.
            Sleep(20);
        } while (GetTickCount64() < deadline);
        error = QStringLiteral("Açık uygulamaya ulaşılamadı. Sistem tepsisindeki w11widget'i kontrol edin.");
        return StartResult::Error;
    }
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        error = nativeError(L"WaitForSingleObject");
        return StartResult::Error;
    }
    ownsMutex_ = true;
    WNDCLASSW definition{};
    definition.lpfnWndProc = windowProcedure;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpszClassName = windowClass;
    if (!RegisterClassW(&definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = nativeError(L"RegisterClass");
        return StartResult::Error;
    }
    window_ = CreateWindowExW(0, windowClass, key.c_str(), 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, definition.hInstance, this);
    if (!window_) { error = nativeError(L"CreateWindow"); return StartResult::Error; }
    return StartResult::Primary;
}

void ApplicationInstance::setActivationHandler(std::function<void()> handler) {
    handler_ = std::move(handler);
    if (pendingSettings_ && handler_) activate(Activation::Settings);
}

void ApplicationInstance::setPanelActivationHandler(std::function<void()> handler) {
    panelHandler_ = std::move(handler);
    if (pendingPanel_ && panelHandler_) activate(Activation::Panel);
}

void ApplicationInstance::activate(Activation action) {
    auto& pending = action == Activation::Panel ? pendingPanel_ : pendingSettings_;
    auto& handler = action == Activation::Panel ? panelHandler_ : handler_;
    pending = !handler;
    if (handler) handler();
}

LRESULT CALLBACK ApplicationInstance::windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* self = reinterpret_cast<ApplicationInstance*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == activateMessage && self) {
        if (wParam != static_cast<WPARAM>(Activation::Settings)
            && wParam != static_cast<WPARAM>(Activation::Panel)) return 0;
        const auto action = static_cast<Activation>(wParam);
        QMetaObject::invokeMethod(self, [self, action] { self->activate(action); }, Qt::QueuedConnection);
        return activationAccepted;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace w11widget::windows
