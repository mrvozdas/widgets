#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <string>

namespace w11widget::native {

// Shares the v2 Qt endpoint so a legacy and native panel cannot own one profile.
class ProfileInstance {
public:
    ~ProfileInstance();
    enum class Result { Primary, Existing, Error };
    Result start(const std::filesystem::path& profile, bool settings, std::wstring& error);
    void onActivation(std::function<void(bool)> callback);
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    HANDLE mutex_ = nullptr;
    HWND endpoint_ = nullptr;
    bool ownsMutex_ = false;
    bool pendingSettings_ = false, pendingPanel_ = false;
    std::function<void(bool)> callback_;
};

class TaskbarEvents {
public:
    ~TaskbarEvents();
    bool observe(HWND taskbar, HWND panel, UINT message);
private:
    static void CALLBACK event(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    HWINEVENTHOOK hook_ = nullptr;
    HWND taskbar_ = nullptr;
    HWND panel_ = nullptr;
    UINT message_ = 0;
};

std::wstring quoteArgument(const std::wstring& argument);
}
