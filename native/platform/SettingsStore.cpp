#include "SettingsStore.h"
#include "TimeZones.h"
#include <Windows.h>
#include <ShlObj.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <memory>
#include <system_error>

namespace w11widget::native {
namespace {
namespace json = winrt::Windows::Data::Json;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};
struct Runtime {
    // C++/WinRT caches activation factories. Starting and stopping the last MTA
    // around each JSON call can invalidate that cache between serialize calls.
    // The app owns its apartment for its lifetime; an uninitialized caller fails.
    bool ok() const {
        APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
        return SUCCEEDED(CoGetApartmentType(&type, &qualifier));
    }
};
JsonValue fromWindows(const json::IJsonValue& value, unsigned depth = 0) {
    if (depth > 64) throw winrt::hresult_invalid_argument();
    switch (value.ValueType()) {
    case json::JsonValueType::Null: return nullptr;
    case json::JsonValueType::Boolean: return value.GetBoolean();
    case json::JsonValueType::Number: return value.GetNumber();
    case json::JsonValueType::String: return std::wstring(value.GetString());
    case json::JsonValueType::Array: {
        JsonArray result; for (const auto& item : value.GetArray()) result.push_back(fromWindows(item, depth + 1)); return result;
    }
    case json::JsonValueType::Object: {
        JsonObject result; for (const auto& item : value.GetObject()) result.emplace(std::wstring(item.Key()), fromWindows(item.Value(), depth + 1)); return result;
    }
    }
    throw winrt::hresult_invalid_argument();
}
json::IJsonValue toWindows(const JsonValue& value, unsigned depth = 0) {
    if (depth > 64) throw winrt::hresult_invalid_argument();
    if (const auto p = std::get_if<bool>(&value.value)) return json::JsonValue::CreateBooleanValue(*p);
    if (const auto p = std::get_if<double>(&value.value)) return json::JsonValue::CreateNumberValue(*p);
    if (const auto p = std::get_if<std::wstring>(&value.value)) return json::JsonValue::CreateStringValue(*p);
    if (const auto p = std::get_if<JsonArray>(&value.value)) {
        json::JsonArray array; for (const auto& item : *p) array.Append(toWindows(item, depth + 1)); return array;
    }
    if (const auto p = std::get_if<JsonObject>(&value.value)) {
        json::JsonObject object; for (const auto& [key, item] : *p) object.Insert(key, toWindows(item, depth + 1)); return object;
    }
    return json::JsonValue::CreateNullValue();
}
std::wstring strictUtf8(const std::string& bytes) {
    if (bytes.empty()) throw winrt::hresult_invalid_argument();
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) throw winrt::hresult_invalid_argument();
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), length) != length) throw winrt::hresult_invalid_argument();
    if (!text.empty() && text.front() == 0xfeff) text.erase(text.begin());
    return text;
}
bool readRevision(const std::filesystem::path& path, SettingsRevision& revision, std::wstring& error) {
    revision = {}; error.clear();
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
        error = configText::readError; return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart < 0) { error = configText::readError; return false; }
    if (size.QuadPart > MaximumSettingsBytes) { error = configText::fileLimit; return false; }
    // Read one byte past the limit as a guard against growth between stat and read.
    std::string bytes(MaximumSettingsBytes + 1, '\0'); DWORD total = 0;
    while (total < bytes.size()) {
        DWORD count = 0;
        if (!ReadFile(file.value, bytes.data() + total, static_cast<DWORD>(bytes.size() - total), &count, nullptr)) { error = configText::readError; return false; }
        if (!count) break;
        total += count;
    }
    if (total > MaximumSettingsBytes) { error = configText::fileLimit; return false; }
    bytes.resize(total); revision.exists = true; revision.bytes = std::move(bytes); return true;
}
double currentUnixMilliseconds() {
    FILETIME now{}; GetSystemTimeAsFileTime(&now); ULARGE_INTEGER ticks{};
    ticks.LowPart = now.dwLowDateTime; ticks.HighPart = now.dwHighDateTime;
    return static_cast<double>(ticks.QuadPart / 10000ULL - 11644473600000ULL);
}
bool normalizeOneShots(PanelConfiguration& settings, std::wstring& error, bool* changed = nullptr) {
    const double now = currentUnixMilliseconds();
    for (auto& widget : settings.widgets) {
        if (widget.typeId != L"countdown") continue;
        auto countdown = countdownSettings(widget);
        if (!normalizeCountdown(countdown, now, error)) return false;
        if (changed && countdown.targetDate != stringValue(widget.settings, L"target_date")) *changed = true;
        widget.settings[L"target_date"] = countdown.targetDate;
    }
    return true;
}
std::wstring uniqueSuffix() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) return {};
    wchar_t text[40]{}; StringFromGUID2(id, text, 40); return text;
}
}

std::filesystem::path defaultSettingsPath() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw))) return {};
    const std::filesystem::path path(raw); CoTaskMemFree(raw); return path / L"w11widget" / L"settings.json";
}
SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}
bool parseConfigurationJson(const std::string& bytes, PanelConfiguration& configuration, bool& migrated, std::wstring& error) {
    error.clear(); migrated = false;
    if (bytes.size() > MaximumSettingsBytes) { error = configText::fileLimit; return false; }
    Runtime runtime;
    if (!runtime.ok()) { error = configText::invalidJson; return false; }
    try {
        const auto root = fromWindows(json::JsonObject::Parse(strictUtf8(bytes)));
        return decodeConfiguration(std::get<JsonObject>(root.value), configuration, migrated, error, isValidTimeZone);
    } catch (const winrt::hresult_error&) { error = configText::invalidJson; return false; }
}
bool serializeConfigurationJson(const PanelConfiguration& configuration, std::string& bytes, std::wstring& error) {
    bytes.clear();
    if (!validateConfiguration(configuration, error, isValidTimeZone)) return false;
    Runtime runtime;
    if (!runtime.ok()) { error = configText::saveError; return false; }
    try {
        std::size_t widgetBytes = 0;
        for (const auto& widget : configuration.widgets) widgetBytes += winrt::to_string(toWindows(widget.settings).Stringify()).size();
        if (widgetBytes > MaximumWidgetSettingsBytes) { error = configText::invalidSettings; return false; }
        bytes = winrt::to_string(toWindows(encodeConfiguration(configuration)).Stringify()) + "\n";
        if (bytes.size() > MaximumSettingsBytes) { bytes.clear(); error = configText::fileLimit; return false; }
        return true;
    } catch (const winrt::hresult_error&) { error = configText::saveError; return false; }
}
SettingsLoadResult SettingsStore::load() const {
    SettingsLoadResult result;
    if (path_.empty() || !path_.is_absolute()) { result.error = configText::invalidPath; return result; }
    if (!readRevision(path_, result.revision, result.error)) return result;
    if (!result.revision.exists) { result.ok = true; result.missing = true; return result; }
    if (!parseConfigurationJson(result.revision.bytes, result.settings, result.migrated, result.error)) return result;
    bool anchoredOneShot = false;
    if (!normalizeOneShots(result.settings, result.error, &anchoredOneShot)) return result;
    result.migrated = result.migrated || anchoredOneShot;
    result.ok = true; return result;
}
SettingsSaveResult SettingsStore::save(const PanelConfiguration& settings, const SettingsRevision& expected) const {
    SettingsSaveResult result;
    if (path_.empty() || !path_.is_absolute()) { result.error = configText::invalidPath; return result; }
    auto normalized = settings;
    // Validate before deriving defaults or creating a directory; malformed input must leave the profile untouched.
    if (!validateConfiguration(normalized, result.error, isValidTimeZone) || !normalizeOneShots(normalized, result.error)) return result;
    std::string bytes;
    if (!serializeConfigurationJson(normalized, bytes, result.error)) return result;
    std::error_code ec; std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) { result.error = configText::saveError; return result; }
    const auto lockPath = std::filesystem::path(path_.wstring() + L".lock");
    Handle writerLock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!writerLock) { result.error = configText::locked; return result; }
    SettingsRevision current;
    if (!readRevision(path_, current, result.error)) return result;
    if (current != expected) { result.conflict = true; result.error = configText::conflict; return result; }
    const auto suffix = uniqueSuffix();
    if (suffix.empty()) { result.error = configText::saveError; return result; }
    const std::filesystem::path temporary(path_.wstring() + L".tmp-" + suffix);
    const std::filesystem::path backup(path_.wstring() + L".bak-" + suffix);
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) { result.error = configText::saveError; return result; }
        DWORD written = 0;
        if (!WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size() || !FlushFileBuffers(file.value)) result.error = configText::saveError;
    }
    if (result.error.empty()) {
        // Check again immediately before replacement; cooperating native writers hold the same lock.
        SettingsRevision latest;
        if (!readRevision(path_, latest, result.error)) {}
        else if (latest != expected) { result.conflict = true; result.error = configText::conflict; }
        else {
            const bool saved = expected.exists ? ReplaceFileW(path_.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr) != 0 :
                MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
            if (saved) {
                result.ok = true; result.revision = {true, std::move(bytes)};
                if (expected.exists) result.backupPath = backup;
            } else {
                const DWORD failure = GetLastError();
                result.error = configText::saveError;
                // ReplaceFile's documented 1177 failure can leave the prior file
                // at the backup name. Restore only into an absent destination;
                // never replace an intervening writer's file during recovery.
                if (failure == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
                    if (!MoveFileExW(backup.c_str(), path_.c_str(), MOVEFILE_WRITE_THROUGH)) {
                        result.backupPath = backup;
                        result.error = std::wstring(configText::backupRecovery) + backup.wstring();
                    }
                }
            }
        }
    }
    if (!result.ok) DeleteFileW(temporary.c_str());
    return result;
}
}
