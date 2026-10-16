#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <SetupAPI.h>
#include <ShlObj.h>
#include <TlHelp32.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "locales/tr.h"
#ifdef W11WIDGET_NATIVE_PACKAGE
#include "NativePackageFiles.h"
#endif

#ifndef W11WIDGET_VERSION
#define W11WIDGET_VERSION L"unknown"
#endif

namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

struct LaunchGate {
    Handle mutex;
    bool acquired = false;
    void release() { if (acquired) { ReleaseMutex(mutex.value); acquired = false; } }
    ~LaunchGate() { release(); }
};

DWORD claimFreshDirectory(const std::filesystem::path& root, std::filesystem::path& target)
{
    std::error_code error;
    const auto parent = root / L"app";
    std::filesystem::create_directories(parent, error);
    if (error) return static_cast<DWORD>(error.value());
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        GUID id{};
        if (FAILED(CoCreateGuid(&id))) return ERROR_GEN_FAILURE;
        wchar_t text[40]{};
        if (!StringFromGUID2(id, text, 40)) return ERROR_GEN_FAILURE;
        const auto candidate = parent / (std::wstring(W11WIDGET_VERSION) + L"-" + std::wstring(text + 1, 36));
        // Claim atomically. An existing directory is never reused, even for
        // another installation of this exact version or a concurrent installer.
        if (std::filesystem::create_directory(candidate, error)) {
            target = candidate;
            return ERROR_SUCCESS;
        }
        if (error) return static_cast<DWORD>(error.value());
    }
    return ERROR_ALREADY_EXISTS;
}

DWORD acquireLaunchGate(const std::filesystem::path& root, LaunchGate& gate)
{
    const Handle directory(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (directory.value == INVALID_HANDLE_VALUE) return GetLastError();
    BY_HANDLE_FILE_INFORMATION identity{};
    if (!GetFileInformationByHandle(directory.value, &identity)) return GetLastError();
    // Directory identity also handles alternate spellings/junctions of the same
    // root. Local mutexes are scoped to this desktop session.
    const auto name = L"Local\\w11widget-setup-" + std::to_wstring(identity.dwVolumeSerialNumber)
        + L"-" + std::to_wstring(identity.nFileIndexHigh) + L"-" + std::to_wstring(identity.nFileIndexLow);
    gate.mutex.value = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!gate.mutex.value) return GetLastError();
    const DWORD wait = WaitForSingleObject(gate.mutex.value, 5000);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        gate.acquired = true;
        return ERROR_SUCCESS;
    }
    return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
}

struct RunningApplication {
    DWORD error = ERROR_SUCCESS;
    DWORD processId = 0;
    std::wstring path;
};

RunningApplication findRunningApplication(const std::filesystem::path& root)
{
    RunningApplication result;
    std::error_code error;
    auto canonicalRoot = std::filesystem::canonical(root, error);
    if (error) { result.error = static_cast<DWORD>(error.value()); return result; }
    auto prefix = canonicalRoot.make_preferred().native();
    if (prefix.back() != L'\\') prefix += L'\\';
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        result.error = GetLastError();
        return result;
    }
    const Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) { result.error = GetLastError(); return result; }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry)) {
        const DWORD code = GetLastError();
        if (code != ERROR_NO_MORE_FILES) result.error = code;
        return result;
    }
    do {
        if (CompareStringOrdinal(entry.szExeFile, -1, L"w11widget.exe", -1, TRUE) != CSTR_EQUAL) continue;
        DWORD candidateSession = 0;
        if (!ProcessIdToSessionId(entry.th32ProcessID, &candidateSession)) {
            const DWORD code = GetLastError();
            if (code != ERROR_INVALID_PARAMETER) result.error = code;
            continue;
        }
        if (candidateSession != session) continue;
        const Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process.value) {
            const DWORD code = GetLastError();
            if (code != ERROR_INVALID_PARAMETER) result.error = code;
            continue;
        }
        DWORD exitCode = 0;
        if (GetExitCodeProcess(process.value, &exitCode) && exitCode != STILL_ACTIVE) continue;
        std::vector<wchar_t> image(32768);
        DWORD length = static_cast<DWORD>(image.size());
        if (!QueryFullProcessImageNameW(process.value, 0, image.data(), &length)) {
            result.error = GetLastError();
            continue;
        }
        auto candidate = std::filesystem::canonical(std::filesystem::path(image.data()), error);
        if (error) { result.error = static_cast<DWORD>(error.value()); continue; }
        const auto& path = candidate.make_preferred().native();
        if (path.size() > prefix.size()
            && CompareStringOrdinal(path.data(), static_cast<int>(prefix.size()),
                                    prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL) {
            result.error = ERROR_SUCCESS;
            result.processId = entry.th32ProcessID;
            result.path = path;
            return result;
        }
    } while (Process32NextW(snapshot.value, &entry));
    const DWORD code = GetLastError();
    if (code != ERROR_NO_MORE_FILES) result.error = code;
    return result;
}

struct ExtractionContext {
    std::filesystem::path targetDirectory;
    std::wstring currentFile;
    DWORD error = ERROR_SUCCESS;
    unsigned extractedFiles = 0;
};

std::wstring windowsError(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                          | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = size && buffer ? std::wstring(buffer, size) : L"Bilinmeyen Windows hatası.";
    if (buffer) LocalFree(buffer);
    return L"Windows " + std::to_wstring(code) + L": " + message;
}

std::string utf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                  result.data(), size, nullptr, nullptr);
    return result;
}

void showError(const std::wstring& message, bool quiet)
{
    OutputDebugStringW(message.c_str());
    if (!quiet) MessageBoxW(nullptr, message.c_str(), L"w11widget — Kurulum hatası", MB_ICONERROR | MB_OK);
}

UINT CALLBACK extractCallback(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR)
{
    auto& extraction = *static_cast<ExtractionContext*>(context);
    if (notification == SPFILENOTIFY_FILEEXTRACTED) {
        const auto* info = reinterpret_cast<const FILEPATHS_W*>(param1);
        if (info->Win32Error != ERROR_SUCCESS) {
            extraction.error = info->Win32Error;
            return info->Win32Error;
        }
        ++extraction.extractedFiles;
        return NO_ERROR;
    }
    if (notification != SPFILENOTIFY_FILEINCABINET) return NO_ERROR;
    auto* const info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(param1);
    extraction.currentFile = info->NameInCabinet;
    const auto relative = std::filesystem::path(info->NameInCabinet).lexically_normal();
    if (relative.has_root_path() || relative.empty() || relative == L"."
        || *relative.begin() == L".." || relative.native().find(L':') != std::wstring::npos) {
        extraction.error = ERROR_INVALID_NAME;
        SetLastError(extraction.error);
        return FILEOP_ABORT;
    }
    const auto target = extraction.targetDirectory / relative;
    if (target.native().size() >= MAX_PATH) {
        extraction.error = ERROR_FILENAME_EXCED_RANGE;
        SetLastError(extraction.error);
        return FILEOP_ABORT;
    }
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
        extraction.error = static_cast<DWORD>(error.value());
        SetLastError(extraction.error);
        return FILEOP_ABORT;
    }
    if (wcscpy_s(info->FullTargetName, MAX_PATH, target.c_str()) != 0) {
        extraction.error = ERROR_INSUFFICIENT_BUFFER;
        SetLastError(extraction.error);
        return FILEOP_ABORT;
    }
    return FILEOP_DOIT;
}

bool writePayload(const std::filesystem::path& path, DWORD& error)
{
    const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(1), L"PAYLOAD");
    if (!resource) { error = GetLastError(); return false; }
    const DWORD size = SizeofResource(nullptr, resource);
    const auto loaded = LoadResource(nullptr, resource);
    const auto data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) { error = ERROR_RESOURCE_DATA_NOT_FOUND; return false; }
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    DWORD written = 0;
    const bool ok = WriteFile(file, data, size, &written, nullptr) != FALSE && written == size;
    error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && error == ERROR_SUCCESS) error = ERROR_WRITE_FAULT;
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

int runInstaller(bool quiet, bool noLaunch, std::filesystem::path targetDirectory,
                 std::filesystem::path installRoot)
{
    if (targetDirectory.empty() && installRoot.empty()) {
        PWSTR localAppData = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
        if (FAILED(result)) {
            showError(L"Yerel uygulama veri klasörü bulunamadı.", quiet);
            return 1;
        }
        installRoot = std::filesystem::path(localAppData) / L"w11widget";
        CoTaskMemFree(localAppData);
    }
    if (!installRoot.empty()) {
        installRoot = installRoot.lexically_normal();
        if (!installRoot.is_absolute() || installRoot == installRoot.root_path()) {
            showError(L"Kurulum için tam bir uygulama klasörü yolu gerekli.", quiet);
            return 2;
        }
        const DWORD error = claimFreshDirectory(installRoot, targetDirectory);
        if (error != ERROR_SUCCESS) {
            showError(L"Yeni kurulum klasörü oluşturulamadı.\n" + windowsError(error), quiet);
            return 1;
        }
    }
    targetDirectory = targetDirectory.lexically_normal();
    if (!targetDirectory.is_absolute() || targetDirectory == targetDirectory.root_path()) {
        showError(L"Kurulum için tam bir klasör yolu gerekli.", quiet);
        return 2;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(targetDirectory.parent_path(), directoryError);
    const auto logPath = targetDirectory.parent_path() / (targetDirectory.filename().wstring() + L"-setup.log");
    std::ofstream log(logPath, std::ios::binary | std::ios::trunc);
    const auto record = [&](const std::wstring& text) {
        if (log) { log << utf8(text) << '\n'; log.flush(); }
    };
    const auto fail = [&](const std::wstring& text) {
        record(text);
        showError(text + L"\n\nKayıt dosyası: " + logPath.wstring(), quiet);
        return 1;
    };
    record(L"w11widget " W11WIDGET_VERSION L"\nHedef: " + targetDirectory.wstring());
    if (directoryError) return fail(L"Kurulum klasörü oluşturulamadı.\n" + windowsError(static_cast<DWORD>(directoryError.value())));

    std::vector<wchar_t> temporaryPath(32768);
    const DWORD count = GetTempPathW(static_cast<DWORD>(temporaryPath.size()), temporaryPath.data());
    if (count == 0 || count >= temporaryPath.size()) return fail(L"Geçici klasör bulunamadı.");
    const auto payloadPath = std::filesystem::path(temporaryPath.data()) /
        (L"w11widget-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".cab");
    DWORD payloadError = 0;
    if (!writePayload(payloadPath, payloadError)) return fail(L"Kurulum paketi okunamadı.\n" + windowsError(payloadError));

    ExtractionContext context{targetDirectory, {}, ERROR_SUCCESS, 0};
    SetLastError(ERROR_SUCCESS);
    const bool extracted = SetupIterateCabinetW(payloadPath.c_str(), 0, extractCallback, &context) != FALSE;
    // Capture before cleanup overwrites GetLastError.
    const DWORD extractionError = extracted ? ERROR_SUCCESS : GetLastError();
    std::error_code ignored;
    std::filesystem::remove(payloadPath, ignored);
    if (!extracted || context.error != ERROR_SUCCESS) {
        const DWORD code = context.error != ERROR_SUCCESS ? context.error : extractionError;
        std::wstring message = L"Uygulama dosyaları yüklenemedi.\nDosya: " + context.currentFile + L"\n" + windowsError(code);
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION)
            message += L"\nBu sürüm açıksa Panel kontrolü penceresinden Çıkış yapıp kurulumu yeniden çalıştırın.";
        return fail(message);
    }
    record(L"Çıkarılan dosya sayısı: " + std::to_wstring(context.extractedFiles));
    const auto application = targetDirectory / L"w11widget.exe";
#ifdef W11WIDGET_NATIVE_PACKAGE
    for (const auto* file : NativePackageFiles) {
        if (!std::filesystem::is_regular_file(targetDirectory / file))
            return fail(std::wstring(w11widget::installer::strings::MissingNativePackage) + L"\n" + file);
    }
#else
    if (!std::filesystem::is_regular_file(application)
        || !std::filesystem::is_regular_file(targetDirectory / L"platforms" / L"qwindows.dll")
        || !std::filesystem::is_directory(targetDirectory / L"qml" / L"QtQuick"))
        return fail(L"Kurulum paketi eksik: uygulama, Windows eklentisi veya QML klasörü bulunamadı.");
#endif
    record(L"Application: " + application.wstring());
    record(L"Status: ready");
    if (noLaunch) { record(L"Launch: skipped-no-launch"); return 0; }
    LaunchGate gate;
    const DWORD gateError = acquireLaunchGate(installRoot, gate);
    const auto running = gateError == ERROR_SUCCESS ? findRunningApplication(installRoot)
                                                    : RunningApplication{gateError, 0, {}};
    if (running.processId || running.error != ERROR_SUCCESS) {
        std::wstring message = L"Kurulum tamamlandı.\n";
        if (running.processId) {
            record(L"Running: PID " + std::to_wstring(running.processId) + L" " + running.path);
            record(L"Launch: skipped-running");
            message += L"w11widget zaten açık. Yeni sürüme geçmek için açık w11widget uygulamalarının "
                       L"Panel kontrolü penceresinden Çıkış yapın, ardından aşağıdaki dosyayı açın:\n\n";
        } else {
            record(L"Launch: skipped-unknown\n" + windowsError(running.error));
            message += L"Açık uygulama durumu doğrulanamadı.\n" + windowsError(running.error)
                     + L"\nYeni sürümü bu dosyadan açabilirsiniz:\n\n";
        }
        message += application.wstring();
        record(message);
        gate.release();
        if (!quiet) MessageBoxW(nullptr, message.c_str(), L"w11widget — Kurulum tamamlandı", MB_ICONINFORMATION | MB_OK);
        return 0;
    }
    std::wstring commandLine = L"\"" + application.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(application.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, targetDirectory.c_str(), &startup, &process)) {
        const DWORD launchError = GetLastError();
        return fail(L"w11widget başlatılamadı.\n" + windowsError(launchError));
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    record(L"Launch: started PID " + std::to_wstring(process.dwProcessId));
    record(L"Kurulum tamamlandı; uygulama başlatıldı.");
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    bool quiet = false;
    bool noLaunch = false;
    std::filesystem::path target;
    std::filesystem::path root;
    bool targetSpecified = false;
    bool rootSpecified = false;
    bool valid = true;
    for (int index = 1; index < count; ++index) {
        const std::wstring argument(arguments[index]);
        if (argument == L"--quiet") quiet = true;
        else if (argument == L"--no-launch") noLaunch = true;
        else if (argument == L"--extract-to" && index + 1 < count) {
            if (targetSpecified) valid = false;
            targetSpecified = true;
            target = arguments[++index];
            if (target.empty()) valid = false;
            noLaunch = true;
        } else if (argument == L"--install-root" && index + 1 < count) {
            if (rootSpecified) valid = false;
            rootSpecified = true;
            root = arguments[++index];
            if (root.empty()) valid = false;
        } else valid = false;
    }
    LocalFree(arguments);
    if (!valid || (targetSpecified && rootSpecified)) {
        showError(L"Geçersiz kurulum seçeneği.", quiet);
        return 2;
    }
    try { return runInstaller(quiet, noLaunch, target, root); }
    catch (const std::exception& exception) {
        const std::string detail(exception.what());
        showError(L"Kurulum tamamlanamadı.\n" + std::wstring(detail.begin(), detail.end()), quiet);
        return 1;
    }
}
