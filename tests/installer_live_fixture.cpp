#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};

bool absolutePath(const std::wstring& path)
{
    return (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        || (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\');
}

int run()
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    std::wstring stopEvent;
    std::wstring holdFile;
    bool valid = true;
    for (int index = 1; index < count; ++index) {
        const std::wstring option(arguments[index]);
        if (option == L"--stop-event" && index + 1 < count && stopEvent.empty()) {
            stopEvent = arguments[++index];
        } else if (option == L"--hold" && index + 1 < count && holdFile.empty()) {
            holdFile = arguments[++index];
        } else valid = false;
    }
    LocalFree(arguments);
    if (!valid || stopEvent.rfind(L"Local\\", 0) != 0 || !absolutePath(holdFile)) return 2;

    const Handle event(OpenEventW(SYNCHRONIZE, FALSE, stopEvent.c_str()));
    if (!event.valid()) return 3;
    const Handle lock(CreateFileW(holdFile.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!lock.valid()) return 4;

    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return 5;
    std::wstring readyPath(executable.data(), length);
    const auto separator = readyPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return 5;
    readyPath.resize(separator + 1);
    readyPath += L"ready.txt";
    {
        const Handle ready(CreateFileW(readyPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                       nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!ready.valid()) return 6;
        const std::string message = "READY PID " + std::to_string(GetCurrentProcessId()) + "\n";
        DWORD written = 0;
        if (!WriteFile(ready.get(), message.data(), static_cast<DWORD>(message.size()), &written, nullptr)
            || written != message.size()) return 7;
    }
    // The owning test signals this event. The helper owns no GUI and changes no
    // state outside its copied executable directory and the explicitly held file.
    return WaitForSingleObject(event.get(), 60000) == WAIT_OBJECT_0 ? 0 : 8;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try { return run(); }
    catch (...) { return 9; }
}
