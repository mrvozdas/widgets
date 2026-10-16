#pragma once

#include "../core/Configuration.h"
#include <filesystem>

namespace w11widget::native {

struct SettingsRevision {
    bool exists = false;
    std::string bytes;
    bool operator==(const SettingsRevision&) const = default;
};
struct SettingsLoadResult {
    PanelConfiguration settings = defaultConfiguration();
    SettingsRevision revision;
    bool ok = false;
    bool missing = false;
    bool migrated = false;
    std::wstring error;
};
struct SettingsSaveResult {
    bool ok = false;
    bool conflict = false;
    SettingsRevision revision;
    std::filesystem::path backupPath;
    std::wstring error;
};
std::filesystem::path defaultSettingsPath();
// JSON/store callers keep a COM/WinRT apartment initialized for their thread's
// lifetime (WinUI and the native panel already do). The adapter never tears it down.
bool parseConfigurationJson(const std::string& bytes, PanelConfiguration& configuration,
                            bool& migrated, std::wstring& error);
bool serializeConfigurationJson(const PanelConfiguration& configuration, std::string& bytes, std::wstring& error);

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path path);
    const std::filesystem::path& filePath() const noexcept { return path_; }
    SettingsLoadResult load() const;
    // Cooperative writers serialize through a profile lock; stale revisions never intentionally overwrite.
    SettingsSaveResult save(const PanelConfiguration& settings, const SettingsRevision& expected) const;
private:
    std::filesystem::path path_;
};

} // namespace w11widget::native
