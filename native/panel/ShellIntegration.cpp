#include "ShellIntegration.h"
#include <bcrypt.h>
#include <icu.h>
#include <algorithm>
#include <map>
#include <vector>

namespace w11widget::native {
namespace {
constexpr wchar_t InstanceClass[] = L"w11widget.ApplicationInstance.v2";
constexpr UINT Activate = WM_APP + 0x251;
constexpr UINT DeliverActivation = WM_APP + 0x252;
constexpr LRESULT Accepted = 0x573131;
thread_local std::map<HWINEVENTHOOK, TaskbarEvents*> observers;

std::wstring profileKey(const std::filesystem::path& path)
{
    auto scope = std::filesystem::absolute(path).lexically_normal().wstring();
    std::replace(scope.begin(), scope.end(), L'\\', L'/');
    UErrorCode foldError = U_ZERO_ERROR;
    const int count = u_strFoldCase(nullptr, 0, reinterpret_cast<const UChar*>(scope.data()),
        static_cast<int32_t>(scope.size()), U_FOLD_CASE_DEFAULT, &foldError);
    if (count <= 0 || (foldError != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(foldError))) return {};
    std::wstring lowered(static_cast<size_t>(count), L'\0');
    foldError = U_ZERO_ERROR;
    u_strFoldCase(reinterpret_cast<UChar*>(lowered.data()), count,
        reinterpret_cast<const UChar*>(scope.data()), static_cast<int32_t>(scope.size()), U_FOLD_CASE_DEFAULT, &foldError);
    if (U_FAILURE(foldError)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, lowered.data(), count,
        nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string utf8(static_cast<size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, lowered.data(), count,
        utf8.data(), bytes, nullptr, nullptr)) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    unsigned char digest[32]{};
    const auto status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(utf8.data()), static_cast<ULONG>(utf8.size()), digest, 32);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {};
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return {};
    std::wstring key = L"w11widget-app-v2-" + std::to_wstring(session) + L"-";
    constexpr wchar_t hex[] = L"0123456789abcdef";
    for (const auto value : digest) { key += hex[value >> 4]; key += hex[value & 15]; }
    return key;
}
}

ProfileInstance::~ProfileInstance()
{
    if (endpoint_) DestroyWindow(endpoint_);
    if (ownsMutex_) ReleaseMutex(mutex_);
    if (mutex_) CloseHandle(mutex_);
}

ProfileInstance::Result ProfileInstance::start(const std::filesystem::path& profile,
    bool settings, std::wstring& error)
{
    const auto key = profileKey(profile);
    if (key.empty()) { error = L"PROFILE_ID_FAILED"; return Result::Error; }
    mutex_ = CreateMutexW(nullptr, FALSE, (L"Local\\" + key).c_str());
    if (!mutex_) { error = L"PROFILE_MUTEX_FAILED"; return Result::Error; }
    const auto wait = WaitForSingleObject(mutex_, 0);
    if (wait == WAIT_TIMEOUT) {
        const auto deadline = GetTickCount64() + 5000;
        do {
            if (const HWND endpoint = FindWindowExW(HWND_MESSAGE, nullptr, InstanceClass, key.c_str())) {
                DWORD pid = 0;
                GetWindowThreadProcessId(endpoint, &pid);
                AllowSetForegroundWindow(pid);
                DWORD_PTR response = 0;
                if (SendMessageTimeoutW(endpoint, Activate, settings ? 0 : 1, 0,
                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &response) && response == Accepted)
                    return Result::Existing;
                error = L"PROFILE_INSTANCE_UNRESPONSIVE";
                return Result::Error;
            }
            Sleep(20);
        } while (GetTickCount64() < deadline);
        error = L"PROFILE_ENDPOINT_UNAVAILABLE";
        return Result::Error;
    }
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        error = L"PROFILE_LOCK_FAILED"; return Result::Error;
    }
    ownsMutex_ = true;
    WNDCLASSW type{};
    type.lpfnWndProc = procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = InstanceClass;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return Result::Error;
    endpoint_ = CreateWindowExW(0, InstanceClass, key.c_str(), 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, type.hInstance, this);
    if (!endpoint_) { error = L"PROFILE_ENDPOINT_FAILED"; return Result::Error; }
    return Result::Primary;
}

LRESULT CALLBACK ProfileInstance::procedure(HWND window, UINT message, WPARAM value, LPARAM parameter)
{
    auto* self = reinterpret_cast<ProfileInstance*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ProfileInstance*>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == Activate && value <= 1) {
        (value == 0 ? self->pendingSettings_ : self->pendingPanel_) = true;
        if (self->callback_) PostMessageW(window, DeliverActivation, 0, 0);
        return Accepted;
    }
    if (self && message == DeliverActivation && self->callback_) {
        const bool settings = self->pendingSettings_, panel = self->pendingPanel_;
        self->pendingSettings_ = false; self->pendingPanel_ = false;
        if (panel) self->callback_(false);
        if (settings) self->callback_(true);
        return 0;
    }
    return DefWindowProcW(window, message, value, parameter);
}

void ProfileInstance::onActivation(std::function<void(bool)> callback)
{
    callback_ = std::move(callback);
    if (pendingSettings_ || pendingPanel_) PostMessageW(endpoint_, DeliverActivation, 0, 0);
}

TaskbarEvents::~TaskbarEvents()
{
    if (hook_) { observers.erase(hook_); UnhookWinEvent(hook_); }
}
bool TaskbarEvents::observe(HWND taskbar, HWND panel, UINT message)
{
    if (taskbar == taskbar_ && hook_) return true;
    if (hook_) { observers.erase(hook_); UnhookWinEvent(hook_); hook_ = nullptr; }
    taskbar_ = taskbar; panel_ = panel; message_ = message;
    DWORD process = 0;
    if (!GetWindowThreadProcessId(taskbar, &process) || !process) return false;
    hook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_REORDER, nullptr, event,
        process, 0, WINEVENT_OUTOFCONTEXT);
    if (hook_) observers.emplace(hook_, this);
    return hook_ != nullptr;
}
void CALLBACK TaskbarEvents::event(HWINEVENTHOOK hook, DWORD eventId, HWND window,
    LONG object, LONG, DWORD, DWORD)
{
    const auto found = observers.find(hook);
    if (found == observers.end()) return;
    const auto& self = *found->second;
    if ((eventId == EVENT_OBJECT_REORDER && window == GetDesktopWindow() && object == OBJID_CLIENT) ||
        (eventId == EVENT_OBJECT_SHOW && window == self.taskbar_ && object == OBJID_WINDOW))
        PostMessageW(self.panel_, self.message_, 0, 0);
}

std::wstring quoteArgument(const std::wstring& argument)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto value : argument) {
        if (value == L'\\') { ++slashes; continue; }
        result.append(slashes * (value == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (value == L'"') result += L'\\';
        result += value;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
}
