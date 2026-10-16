#include "SettingsStore.h"
#include <Windows.h>
#include <objbase.h>
#include <winrt/base.h>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace w11widget::native;
namespace {
struct Fixture {
    std::filesystem::path directory;
    Fixture() {
        wchar_t temporary[MAX_PATH]{}; GUID id{}; wchar_t identifier[40]{};
        if (!GetTempPathW(MAX_PATH, temporary) || FAILED(CoCreateGuid(&id))) throw std::runtime_error("temporary directory unavailable");
        StringFromGUID2(id, identifier, 40); directory = std::filesystem::path(temporary) / (std::wstring(L"w11widget-native-test-") + identifier);
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("temporary directory collision");
    }
    ~Fixture() { std::error_code ec; std::filesystem::remove_all(directory, ec); }
};
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary); return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc); stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("fixture write failed");
}
}
int main() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    int checks = 0;
    std::cout << "Native persistence test started" << std::endl;
    const auto check = [&](bool condition, const char* name) {
        ++checks;
        if (!condition) throw std::runtime_error(name);
        std::cout << "PASS " << checks << ": " << name << std::endl;
    };
    try {
        Fixture fixture; SettingsStore store(fixture.directory / L"profile" / L"settings.json");
        auto loaded = store.load(); check(loaded.ok && loaded.missing && !loaded.revision.exists && loaded.settings.widgets.size() == 4, "missing profile is read-only defaults");
        check(!std::filesystem::exists(store.filePath().parent_path()), "load did not create profile directory");
        auto saved = store.save(loaded.settings, loaded.revision); check(saved.ok && saved.backupPath.empty(), "first atomic save");
        const auto original = read(store.filePath()); check(original == saved.revision.bytes, "revision matches exact disk bytes");
        loaded = store.load(); check(loaded.ok && !loaded.missing && !loaded.migrated, "saved schema2 reload");
        auto changed = loaded.settings;
        changed.extra[L"future_root"] = JsonObject{{L"nested", JsonArray{L"Türkçe", nullptr, 42}}};
        auto unknown = defaultWidget(L"future-widget", L"future-1"); unknown.settings[L"secret-free-fixture"] = JsonArray{1, false}; changed.widgets.push_back(unknown);
        changed.theme = L"dark";
        saved = store.save(changed, loaded.revision); check(saved.ok && !saved.backupPath.empty() && read(saved.backupPath) == original, "replacement keeps byte-identical prior-file backup");
        auto current = store.load(); check(current.ok && current.settings == changed, "unknown data and theme roundtrip through Windows JSON");
        const auto stable = read(store.filePath());
        auto stale = store.save(loaded.settings, loaded.revision); check(!stale.ok && stale.conflict && read(store.filePath()) == stable, "stale editor cannot overwrite new data");
        auto invalid = changed; invalid.widgets[0].widthDip = 1;
        check(!store.save(invalid, current.revision).ok && read(store.filePath()) == stable, "invalid save leaves existing bytes untouched");
        SettingsStore newStore(fixture.directory / L"must-not-exist" / L"settings.json");
        check(!newStore.save(invalid, {}).ok && !std::filesystem::exists(newStore.filePath().parent_path()), "invalid save creates no directory");
        HANDLE locked = CreateFileW((store.filePath().wstring() + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "fixture writer lock");
        auto blocked = store.save(changed, current.revision); CloseHandle(locked);
        check(!blocked.ok && read(store.filePath()) == stable, "simultaneous writer lock refuses mutation");
        auto first = changed; first.offsetDip = 151; auto second = changed; second.offsetDip = 152;
        auto worker = std::async(std::launch::async, [&] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const auto result = store.save(first, current.revision);
            winrt::uninit_apartment();
            return result;
        });
        const auto secondResult = store.save(second, current.revision); const auto firstResult = worker.get();
        check(firstResult.ok != secondResult.ok, "two concurrent editors produce exactly one commit");
        current = store.load(); check(current.ok && (current.settings.offsetDip == 151 || current.settings.offsetDip == 152), "concurrent commit remains valid JSON");
        HANDLE denyReplace = CreateFileW(store.filePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(denyReplace != INVALID_HANDLE_VALUE, "fixture deny replacement handle");
        const auto heldBytes = read(store.filePath()); const auto refused = store.save(current.settings, current.revision); CloseHandle(denyReplace);
        check(!refused.ok && read(store.filePath()) == heldBytes, "failed atomic replacement preserves prior bytes");
        current.settings.widgets[0].settings[L"daily_repeat"] = false; current.settings.widgets[0].settings[L"target_date"] = L"";
        saved = store.save(current.settings, current.revision); current = store.load();
        check(saved.ok && current.ok && validDate(stringValue(current.settings.widgets[0].settings, L"target_date")), "one-shot date is persisted once");
        const auto singleDate = stringValue(current.settings.widgets[0].settings, L"target_date");
        saved = store.save(current.settings, current.revision); check(saved.ok && stringValue(store.load().settings.widgets[0].settings, L"target_date") == singleDate, "saved one-shot date remains stable");
        current.settings.widgets[0].settings[L"target_date"] = L"";
        std::string legacyOneShot; std::wstring oneShotError;
        check(serializeConfigurationJson(current.settings, legacyOneShot, oneShotError), "legacy undated one-shot fixture");
        write(store.filePath(), legacyOneShot);
        current = store.load();
        check(current.ok && current.migrated && validDate(stringValue(current.settings.widgets[0].settings, L"target_date")) &&
              read(store.filePath()) == legacyOneShot, "one-shot anchoring enables Apply without rewriting during load");
        const auto beforeMalformed = store.load().revision;
        write(store.filePath(), "{broken"); check(!store.load().ok, "malformed JSON is reported");
        check(store.save(changed, beforeMalformed).conflict && read(store.filePath()) == "{broken", "external malformed edit is not replaced");
        write(store.filePath(), std::string(MaximumSettingsBytes + 1, ' ')); check(!store.load().ok, "oversized input rejected before parse");
        PanelConfiguration parsed; bool migrated = false; std::wstring error;
        check(!parseConfigurationJson(std::string("\xff", 1), parsed, migrated, error), "invalid UTF8 rejected");
        check(!parseConfigurationJson("[]", parsed, migrated, error), "array root rejected");
        const std::string legacy = R"({"schema_version":2,"panel":{"monitor":"primary","side":"left","width_dip":330,"offset_dip":150,"opacity":0.95,"hide_for_fullscreen":false},"widgets":[{"instance_id":"clock-1","type_id":"world-clock","enabled":true,"visible":true,"width_dip":160,"settings":{"show_date":false,"time_zones":["UTC","Asia/Tokyo"]}}]})";
        check(parseConfigurationJson(legacy, parsed, migrated, error) && migrated && parsed.widgets.size() == 2, "legacy grouped clock Windows JSON migration");
        check(defaultSettingsPath().filename() == L"settings.json" && defaultSettingsPath().parent_path().filename() == L"w11widget", "default path contract without reading user profile");
        std::cout << "Native Windows persistence: " << checks << " checks PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL at check " << checks << ": " << error.what() << '\n'; return 1; }
}
