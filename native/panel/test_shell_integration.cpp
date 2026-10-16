#include "ShellIntegration.h"
#include <future>
#include <iostream>

int main()
{
    using w11widget::native::ProfileInstance;
    unsigned checks = 0;
    const auto check = [&](bool value, const char* message) {
        ++checks;
        if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    };
    check(w11widget::native::quoteArgument(L"C:\\path with space\\") == L"\"C:\\path with space\\\\\"",
        "trailing slash is doubled before the closing quote");
    check(w11widget::native::quoteArgument(L"quoted\"text") == L"\"quoted\\\"text\"",
        "embedded quote cannot escape the argument");
    ProfileInstance primary;
    std::wstring error;
    check(primary.start(L"C:\\w11widget-tests\\Straße\\settings.json", false, error) == ProfileInstance::Result::Primary,
        "first instance owns its isolated profile");
    DWORD session = 0;
    check(ProcessIdToSessionId(GetCurrentProcessId(), &session) != FALSE, "session is available");
    // SHA-256 of the UTF-8 Qt-casefolded C:/w11widget-tests/Straße/settings.json.
    const auto key = L"w11widget-app-v2-" + std::to_wstring(session) +
        L"-f2dde66c6c4b0db1f2f89b906bc0539fd8c207e8ef9fee99f0c44e8bdc8c53de";
    check(FindWindowExW(HWND_MESSAGE, nullptr, L"w11widget.ApplicationInstance.v2", key.c_str()) != nullptr,
        "profile endpoint matches Qt case folding including German sharp-s");
    const auto activate = [&](bool settings) {
        auto pending = std::async(std::launch::async, [settings] {
            ProfileInstance second;
            std::wstring detail;
            return second.start(L"c:/w11widget-tests/STRASSE/settings.json", settings, detail);
        });
        const auto deadline = GetTickCount64() + 6000;
        while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && GetTickCount64() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
            Sleep(1);
        }
        check(pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready &&
            pending.get() == ProfileInstance::Result::Existing, "second instance activates the matching profile");
    };
    activate(true);
    activate(false);
    bool settingsRequested = false, panelRequested = false;
    primary.onActivation([&](bool settings) { (settings ? settingsRequested : panelRequested) = true; });
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    check(settingsRequested && panelRequested, "requests received during startup are delivered after initialization");
    std::cout << "Native shell integration: " << checks << " checks PASS\n";
}
