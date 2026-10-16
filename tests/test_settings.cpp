#include "core/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

bool writeBytes(const QString& path, const QByteArray& contents) {
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    check(opened, "test fixture file opens in temporary directory");
    if (!opened) return false;
    const bool written = file.write(contents) == contents.size();
    check(written, "test fixture bytes are written completely");
    return written;
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "temporary fixture can be read back");
    return file.readAll();
}

QJsonObject validDocument() {
    const QJsonObject panel{
        {QStringLiteral("monitor"), QStringLiteral("primary")},
        {QStringLiteral("side"), QStringLiteral("left")},
        {QStringLiteral("width_dip"), 330},
        {QStringLiteral("offset_dip"), 150},
        {QStringLiteral("opacity"), 0.95},
        {QStringLiteral("hide_for_fullscreen"), false}
    };
    QJsonArray widgets;
    for (const auto& widget : w11widget::defaultWidgetConfigurations()) {
        widgets.append(QJsonObject{
            {QStringLiteral("instance_id"), widget.instanceId},
            {QStringLiteral("type_id"), widget.typeId},
            {QStringLiteral("enabled"), widget.enabled},
            {QStringLiteral("visible"), widget.visible},
            {QStringLiteral("width_dip"), widget.widthDip},
            {QStringLiteral("settings"), widget.settings}
        });
    }
    return {{QStringLiteral("schema_version"), 2}, {QStringLiteral("panel"), panel},
             {QStringLiteral("widgets"), widgets}};
}

void expectInvalidDocument(const w11widget::SettingsStore& store, const QByteArray& bytes,
                           const char* description) {
    if (!writeBytes(store.filePath(), bytes)) return;
    const auto result = store.load();
    check(!result.ok && !result.missing && !result.error.isEmpty(), description);
    check(result.settings == w11widget::PanelSettings{}, "invalid load returns complete safe defaults");
    check(readBytes(store.filePath()) == bytes, "invalid load leaves the original file unchanged");
}

void testRoundtrip(const QString& root) {
    using namespace w11widget;
    const QString path = QDir(root).filePath(QStringLiteral("nested/preferences/settings.json"));
    SettingsStore store(path);
    check(store.filePath() == path, "SettingsStore preserves its explicitly supplied path");
    const auto missing = store.load();
    check(missing.ok && missing.missing && missing.error.isEmpty() && missing.settings == PanelSettings{},
          "missing settings return approved defaults without an error");
    check(!QFileInfo::exists(path) && !QFileInfo::exists(QFileInfo(path).absolutePath()),
          "loading missing settings does not create a file or parent directory");

    PanelSettings settings;
    settings.monitor = QStringLiteral("\\\\.\\DISPLAY2");
    settings.side = PanelSide::Right;
    settings.widthDip = 512;
    settings.offsetDip = 987;
    settings.opacity = 0.625;
    settings.hideForFullscreen = false;
    settings.widgets = {
        {QStringLiteral("unknown-2"), QStringLiteral("future.widget"), false, true, 320,
         {{QStringLiteral("nested"), QJsonObject{{QStringLiteral("label"), QStringLiteral("Özel")}}},
          {QStringLiteral("values"), QJsonArray{1, true, QStringLiteral("retained")}}}},
        {QStringLiteral("sample-1"), QStringLiteral("sample"), true, false, 48,
         {{QStringLiteral("label"), QStringLiteral("Hidden sample")}}}
    };
    QString error = QStringLiteral("stale-error");
    check(store.save(settings, error) && error.isEmpty(), "valid save creates parents and clears old errors");
    const auto loaded = store.load();
    check(loaded.ok && !loaded.missing && loaded.error.isEmpty() && loaded.settings == settings,
          "all persisted preferences survive a complete atomic save/load roundtrip");
    const auto saved = QJsonDocument::fromJson(readBytes(path)).object();
    check(saved.value(QStringLiteral("schema_version")).toInt() == 2 &&
          saved.value(QStringLiteral("panel")).isObject() &&
          saved.value(QStringLiteral("widgets")).isArray(), "saved settings use the documented versioned schema");
    check(loaded.settings.widgets == settings.widgets,
          "widget order, unknown types, enabled/visible flags and nested settings roundtrip exactly");

    settings.monitor = QStringLiteral("primary");
    settings.side = PanelSide::Left;
    settings.widthDip = 80;
    settings.offsetDip = 0;
    settings.opacity = 0.25;
    settings.hideForFullscreen = false;
    check(store.save(settings, error) && store.load().settings == settings,
          "minimum supported values roundtrip");
    settings.widthDip = 640;
    settings.offsetDip = 20000;
    settings.opacity = 1.0;
    check(store.save(settings, error) && store.load().settings == settings,
          "maximum supported values roundtrip and replace earlier valid settings");
    settings.widgets.clear();
    check(store.save(settings, error) && store.load().settings == settings,
          "an explicitly empty widget list roundtrips without restoring defaults");
}

void testFileSizeLimit(const QString& root) {
    w11widget::SettingsStore store(QDir(root).filePath(QStringLiteral("oversized.json")));
    const QByteArray valid = QJsonDocument(validDocument()).toJson();
    QByteArray boundary = valid;
    boundary.append(QByteArray(256 * 1024 - boundary.size(), ' '));
    check(writeBytes(store.filePath(), boundary) && store.load().ok,
          "a valid settings document at the total file-size limit remains readable");
    boundary.append(' ');
    expectInvalidDocument(store, boundary, "oversized settings are rejected before JSON parsing");
    check(store.load().error.contains(QStringLiteral("256 KiB")),
          "an oversized settings file reports the byte limit explicitly");
    QJsonObject nested;
    for (int depth = 0; depth < 400; ++depth) nested = {{QStringLiteral("nested"), nested}};
    w11widget::PanelSettings settings;
    settings.widgets = {{QStringLiteral("nested-1"), QStringLiteral("future.widget"), false, false, 72, nested}};
    QString error;
    check(!store.save(settings, error) && error.contains(QStringLiteral("256 KiB")) &&
              readBytes(store.filePath()) == boundary,
          "a compact object whose indented file exceeds the read limit cannot replace saved settings");
}

void testLegacyFullscreenMigration(const QString& root) {
    using namespace w11widget;
    SettingsStore store(QDir(root).filePath(QStringLiteral("legacy-fullscreen.json")));
    auto document = validDocument();
    document.insert(QStringLiteral("schema_version"), 1);
    document.remove(QStringLiteral("widgets"));
    auto panel = document.value(QStringLiteral("panel")).toObject();
    panel.insert(QStringLiteral("monitor"), QStringLiteral("\\\\.\\DISPLAY3"));
    panel.insert(QStringLiteral("side"), QStringLiteral("right"));
    panel.insert(QStringLiteral("width_dip"), 456);
    panel.insert(QStringLiteral("offset_dip"), 72);
    panel.insert(QStringLiteral("opacity"), 0.627);
    panel.insert(QStringLiteral("hide_for_fullscreen"), true);
    document.insert(QStringLiteral("panel"), panel);
    const QByteArray original = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (!writeBytes(store.filePath(), original)) return;

    PanelSettings expected;
    expected.monitor = QStringLiteral("\\\\.\\DISPLAY3");
    expected.side = PanelSide::Right;
    expected.widthDip = 456;
    expected.offsetDip = 72;
    expected.opacity = 0.627;
    expected.hideForFullscreen = false;
    const auto loaded = store.load();
    check(loaded.ok && !loaded.missing && loaded.error.isEmpty() && loaded.settings == expected,
          "legacy fullscreen hiding normalizes to false while all other preferences remain intact");
    check(readBytes(store.filePath()) == original,
          "loading the legacy policy never changes the original settings bytes");
    check(loaded.settings.widgets == defaultWidgetConfigurations(),
          "schema one migration adds default widgets only in memory");

    // Schema one never defined widgets. Treat this key just like other unknown
    // legacy root keys instead of interpreting a partial future configuration.
    document.insert(QStringLiteral("widgets"), QStringLiteral("ignored legacy metadata"));
    const QByteArray legacyExtras = QJsonDocument(document).toJson(QJsonDocument::Indented);
    check(writeBytes(store.filePath(), legacyExtras) && store.load().ok &&
          store.load().settings == expected && readBytes(store.filePath()) == legacyExtras,
          "unknown schema one widgets key is ignored without rewriting legacy bytes");

    // Even a caller holding an old in-memory true value cannot re-enable the
    // removed policy when the user later explicitly saves an edited preference.
    PanelSettings requested = expected;
    requested.offsetDip = 73;
    requested.hideForFullscreen = true;
    QString error;
    check(store.save(requested, error) && error.isEmpty(), "explicit save accepts and migrates old in-memory preferences");
    check(requested.hideForFullscreen, "save does not mutate its caller's settings object");
    const auto saved = QJsonDocument::fromJson(readBytes(store.filePath())).object();
    const auto savedPanel = saved.value(QStringLiteral("panel")).toObject();
    const auto persistedFullscreen = savedPanel.value(QStringLiteral("hide_for_fullscreen"));
    check(saved.value(QStringLiteral("schema_version")).toInt() == 2 &&
          persistedFullscreen.isBool() && !persistedFullscreen.toBool(),
          "explicit save migrates to schema two and always writes fullscreen hiding false");
    requested.hideForFullscreen = false;
    const auto roundtrip = store.load();
    check(roundtrip.ok && roundtrip.settings == requested,
          "legacy-true save/load preserves every other edited preference and normalizes only fullscreen hiding");
}

void testInvalidJson(const QString& root) {
    using namespace w11widget;
    SettingsStore store(QDir(root).filePath(QStringLiteral("invalid.json")));
    for (const QByteArray& bytes : std::vector<QByteArray>{"", "{broken", "[]", "null",
                                                          "{\"schema_version\":1,\"panel\":{}}"}) {
        expectInvalidDocument(store, bytes, "malformed, nonobject or incomplete JSON is rejected");
    }
    auto document = validDocument();
    document.remove(QStringLiteral("schema_version"));
    expectInvalidDocument(store, QJsonDocument(document).toJson(), "missing schema version is rejected");
    for (const QJsonValue version : std::vector<QJsonValue>{0, 3, 1.5, 1e100, true, "1", QJsonValue{}}) {
        document = validDocument();
        document.insert(QStringLiteral("schema_version"), version);
        expectInvalidDocument(store, QJsonDocument(document).toJson(), "unknown or noninteger schema version is rejected");
    }
    document = validDocument();
    document.insert(QStringLiteral("panel"), QJsonArray{});
    expectInvalidDocument(store, QJsonDocument(document).toJson(), "panel must be an object");

    const std::vector<std::pair<QString, QJsonValue>> invalidFields{
        {QStringLiteral("monitor"), 1},
        {QStringLiteral("monitor"), QStringLiteral("secondary")},
        {QStringLiteral("monitor"), QStringLiteral("\\\\.\\DISPLAY0")},
        {QStringLiteral("monitor"), QStringLiteral("\\\\.\\DISPLAY1/other")},
        {QStringLiteral("monitor"), QStringLiteral("\\\\.\\DISPLAY") + QString(257, QLatin1Char('1'))},
        {QStringLiteral("side"), true},
        {QStringLiteral("side"), QStringLiteral("bottom")},
        {QStringLiteral("width_dip"), 79},
        {QStringLiteral("width_dip"), 641},
        {QStringLiteral("width_dip"), 330.5},
        {QStringLiteral("width_dip"), QStringLiteral("330")},
        {QStringLiteral("width_dip"), true},
        {QStringLiteral("width_dip"), 1e100},
        {QStringLiteral("offset_dip"), -1},
        {QStringLiteral("offset_dip"), 20001},
        {QStringLiteral("offset_dip"), 150.5},
        {QStringLiteral("offset_dip"), QStringLiteral("150")},
        {QStringLiteral("opacity"), 0.249},
        {QStringLiteral("opacity"), 1.001},
        {QStringLiteral("opacity"), QStringLiteral("0.95")},
        {QStringLiteral("opacity"), true},
        {QStringLiteral("opacity"), QJsonValue{}},
        {QStringLiteral("hide_for_fullscreen"), 1},
        {QStringLiteral("hide_for_fullscreen"), QStringLiteral("true")}
    };
    for (const auto& [key, value] : invalidFields) {
        document = validDocument();
        auto panel = document.value(QStringLiteral("panel")).toObject();
        panel.insert(key, value);
        document.insert(QStringLiteral("panel"), panel);
        expectInvalidDocument(store, QJsonDocument(document).toJson(), "invalid field type or range is rejected");
    }
    const auto requiredFields = validDocument().value(QStringLiteral("panel")).toObject().keys();
    for (const QString& key : requiredFields) {
        document = validDocument();
        auto panel = document.value(QStringLiteral("panel")).toObject();
        panel.remove(key);
        document.insert(QStringLiteral("panel"), panel);
        expectInvalidDocument(store, QJsonDocument(document).toJson(), "every documented panel field is required");
    }
    // JSON cannot represent NaN/Infinity. Such input must fail parsing rather
    // than gaining a default numeric value through implicit conversion.
    auto bytes = QJsonDocument(validDocument()).toJson(QJsonDocument::Compact);
    bytes.replace("0.95", "NaN");
    expectInvalidDocument(store, bytes, "NaN token cannot become an accepted opacity");
    bytes.replace("NaN", "Infinity");
    expectInvalidDocument(store, bytes, "Infinity token cannot become an accepted opacity");

    document = validDocument();
    document.insert(QStringLiteral("future_metadata"), QStringLiteral("ignored"));
    auto panel = document.value(QStringLiteral("panel")).toObject();
    panel.insert(QStringLiteral("future_option"), 123);
    document.insert(QStringLiteral("panel"), panel);
    check(writeBytes(store.filePath(), QJsonDocument(document).toJson()) && store.load().ok,
          "unknown extra keys remain compatible within the supported schema version");
}

void testWidgetValidation(const QString& root) {
    using namespace w11widget;
    SettingsStore store(QDir(root).filePath(QStringLiteral("invalid-widgets.json")));
    auto document = validDocument();
    document.remove(QStringLiteral("widgets"));
    expectInvalidDocument(store, QJsonDocument(document).toJson(), "schema two requires a widget array");
    for (const QJsonValue value : std::vector<QJsonValue>{QJsonObject{}, false, 1, QStringLiteral("widgets"), QJsonValue{}}) {
        document = validDocument();
        document.insert(QStringLiteral("widgets"), value);
        expectInvalidDocument(store, QJsonDocument(document).toJson(), "widgets must be an array");
    }
    document = validDocument();
    document.insert(QStringLiteral("widgets"), QJsonArray{false});
    expectInvalidDocument(store, QJsonDocument(document).toJson(), "widget array entries must be objects");
    const std::vector<std::pair<QString, QJsonValue>> invalidFields{
        {QStringLiteral("instance_id"), QString{}},
        {QStringLiteral("instance_id"), 12},
        {QStringLiteral("instance_id"), QStringLiteral("with space")},
        {QStringLiteral("instance_id"), QStringLiteral("özel")},
        {QStringLiteral("instance_id"), QString(65, QLatin1Char('a'))},
        {QStringLiteral("type_id"), QString{}},
        {QStringLiteral("type_id"), false},
        {QStringLiteral("type_id"), QStringLiteral("path/type")},
        {QStringLiteral("type_id"), QString(65, QLatin1Char('b'))},
        {QStringLiteral("enabled"), 1},
        {QStringLiteral("enabled"), QStringLiteral("true")},
        {QStringLiteral("visible"), 0},
        {QStringLiteral("visible"), QStringLiteral("false")},
        {QStringLiteral("width_dip"), 47},
        {QStringLiteral("width_dip"), 321},
        {QStringLiteral("width_dip"), 120.5},
        {QStringLiteral("width_dip"), true},
        {QStringLiteral("width_dip"), QStringLiteral("120")},
        {QStringLiteral("width_dip"), 1e100},
        {QStringLiteral("settings"), QJsonArray{}},
        {QStringLiteral("settings"), QStringLiteral("{}")},
        {QStringLiteral("settings"), QJsonValue{}}
    };
    for (const auto& [key, value] : invalidFields) {
        document = validDocument();
        auto widgets = document.value(QStringLiteral("widgets")).toArray();
        auto widget = widgets.at(0).toObject();
        widget.insert(key, value);
        widgets.replace(0, widget);
        document.insert(QStringLiteral("widgets"), widgets);
        expectInvalidDocument(store, QJsonDocument(document).toJson(), "invalid widget field type or range is rejected");
    }
    const auto requiredFields = validDocument().value(QStringLiteral("widgets")).toArray().at(0).toObject().keys();
    for (const auto& key : requiredFields) {
        document = validDocument();
        auto widgets = document.value(QStringLiteral("widgets")).toArray();
        auto widget = widgets.at(0).toObject();
        widget.remove(key);
        widgets.replace(0, widget);
        document.insert(QStringLiteral("widgets"), widgets);
        expectInvalidDocument(store, QJsonDocument(document).toJson(), "every widget field is required in schema two");
    }
    document = validDocument();
    auto widgets = document.value(QStringLiteral("widgets")).toArray();
    widgets.append(widgets.first());
    document.insert(QStringLiteral("widgets"), widgets);
    expectInvalidDocument(store, QJsonDocument(document).toJson(), "duplicate widget instance IDs are rejected");
    widgets = {};
    const auto baseline = validDocument().value(QStringLiteral("widgets")).toArray().at(0).toObject();
    for (int index = 0; index < MaximumWidgetCount + 1; ++index) {
        auto widget = baseline;
        widget.insert(QStringLiteral("instance_id"), QStringLiteral("widget-%1").arg(index));
        widgets.append(widget);
    }
    document.insert(QStringLiteral("widgets"), widgets);
    expectInvalidDocument(store, QJsonDocument(document).toJson(), "more than sixteen widgets are rejected");
    widgets.removeLast();
    document.insert(QStringLiteral("widgets"), widgets);
    check(writeBytes(store.filePath(), QJsonDocument(document).toJson()) && store.load().ok &&
          store.load().settings.widgets.size() == MaximumWidgetCount,
          "sixteen distinct widget configurations are accepted");

    PanelSettings settings;
    settings.widgets = {
        {QStringLiteral("settings-fixture-1"), QStringLiteral("sample"), true, true, 120, {}},
        {QStringLiteral("settings-fixture-2"), QStringLiteral("sample"), true, true, 120, {}}
    };
    const auto emptySettingsBytes = QJsonDocument(QJsonObject{{QStringLiteral("data"), QString{}}})
                                      .toJson(QJsonDocument::Compact).size();
    const auto halfLimitPayload = static_cast<int>(32768 - emptySettingsBytes);
    settings.widgets[0].settings = {{QStringLiteral("data"), QString(halfLimitPayload, QLatin1Char('a'))}};
    settings.widgets[1].settings = settings.widgets[0].settings;
    QString error;
    check(store.save(settings, error) && store.load().settings == settings,
          "exactly 64 KiB of compact UTF-8 settings across all widgets is accepted");
    settings.widgets[1].settings.insert(QStringLiteral("data"), QString(halfLimitPayload + 1, QLatin1Char('a')));
    const QByteArray baselineBytes = readBytes(store.filePath());
    check(!store.save(settings, error) && !error.isEmpty() && readBytes(store.filePath()) == baselineBytes,
          "aggregate settings size limit rejects one additional byte before touching the file");
    document = QJsonDocument::fromJson(baselineBytes).object();
    widgets = document.value(QStringLiteral("widgets")).toArray();
    auto oversizedWidget = widgets.at(0).toObject();
    oversizedWidget.insert(QStringLiteral("settings"), QJsonObject{{QStringLiteral("data"),
                           QString(32768, QChar(0x011f))}});
    widgets.replace(0, oversizedWidget);
    document.insert(QStringLiteral("widgets"), widgets);
    expectInvalidDocument(store, QJsonDocument(document).toJson(),
                          "loading bounds total serialized UTF-8 bytes rather than character count");
}

void testInvalidSave(const QString& root) {
    using namespace w11widget;
    SettingsStore store(QDir(root).filePath(QStringLiteral("preserved.json")));
    QString error;
    check(store.save(PanelSettings{}, error), "valid baseline file is saved before invalid-save tests");
    const QByteArray baseline = readBytes(store.filePath());
    std::vector<PanelSettings> invalid;
    auto add = [&invalid](auto mutate) {
        PanelSettings settings;
        settings.widgets.push_back({QStringLiteral("settings-fixture-2"), QStringLiteral("sample"), true, true, 120,
                                    {{QStringLiteral("label"), QStringLiteral("Ayar fikstürü")},
                                     {QStringLiteral("accent"), QStringLiteral("#8ddbc2")}}});
        mutate(settings);
        invalid.push_back(settings);
    };
    add([](PanelSettings& s) { s.monitor.clear(); });
    add([](PanelSettings& s) { s.monitor = QStringLiteral("\\\\.\\DISPLAY-1"); });
    add([](PanelSettings& s) { s.monitor = QStringLiteral("\\\\.\\DISPLAY") + QString(257, QLatin1Char('1')); });
    add([](PanelSettings& s) { s.side = static_cast<PanelSide>(99); });
    add([](PanelSettings& s) { s.widthDip = 79; });
    add([](PanelSettings& s) { s.widthDip = 641; });
    add([](PanelSettings& s) { s.offsetDip = -1; });
    add([](PanelSettings& s) { s.offsetDip = 20001; });
    add([](PanelSettings& s) { s.opacity = 0.249; });
    add([](PanelSettings& s) { s.opacity = 1.001; });
    add([](PanelSettings& s) { s.opacity = std::numeric_limits<double>::quiet_NaN(); });
    add([](PanelSettings& s) { s.opacity = std::numeric_limits<double>::infinity(); });
    add([](PanelSettings& s) { s.opacity = -std::numeric_limits<double>::infinity(); });
    add([](PanelSettings& s) { s.widgets[0].instanceId.clear(); });
    add([](PanelSettings& s) { s.widgets[0].typeId = QStringLiteral("invalid/type"); });
    add([](PanelSettings& s) { s.widgets[1].instanceId = s.widgets[0].instanceId; });
    add([](PanelSettings& s) { s.widgets[0].widthDip = 47; });
    add([](PanelSettings& s) { s.widgets[0].widthDip = 321; });
    add([](PanelSettings& s) { s.widgets.resize(MaximumWidgetCount + 1); });
    add([](PanelSettings& s) { s.widgets[0].settings = {{QStringLiteral("data"), QString(65536, QLatin1Char('a'))}}; });
    for (const auto& settings : invalid) {
        check(!store.save(settings, error) && !error.isEmpty(), "invalid native preferences cannot be saved");
        check(readBytes(store.filePath()) == baseline, "invalid save preserves every byte of the prior file");
    }
    const QByteArray malformed("{malformed-user-settings");
    check(writeBytes(store.filePath(), malformed), "malformed preservation fixture is created");
    check(!store.save(invalid.front(), error) && readBytes(store.filePath()) == malformed,
          "invalid save also preserves a preexisting malformed file");
    SettingsStore nested(QDir(root).filePath(QStringLiteral("never-created/deeper/settings.json")));
    check(!nested.save(invalid.front(), error) &&
          !QFileInfo::exists(QDir(root).filePath(QStringLiteral("never-created"))),
          "invalid save performs no directory creation");
}

void testIoFailuresAndLocation(const QString& root) {
    using namespace w11widget;
    SettingsStore directoryStore(root);
    const auto result = directoryStore.load();
    check(!result.ok && !result.missing && !result.error.isEmpty(), "loading a directory reports an I/O error");
    QString error;
    check(!directoryStore.save(PanelSettings{}, error) && !error.isEmpty() && QFileInfo(root).isDir(),
          "directory target is preserved when atomic save fails");
    const QString obstacle = QDir(root).filePath(QStringLiteral("not-a-directory"));
    check(writeBytes(obstacle, "keep-me"), "parent-directory obstacle fixture is created");
    SettingsStore blocked(QDir(obstacle).filePath(QStringLiteral("settings.json")));
    check(!blocked.save(PanelSettings{}, error) && !error.isEmpty() && readBytes(obstacle) == "keep-me",
          "uncreatable parent reports failure and preserves the obstacle file");
    SettingsStore empty(QString{});
    check(!empty.load().ok && !empty.save(PanelSettings{}, error) && !error.isEmpty(),
          "empty paths produce explicit failures rather than current-directory writes");
    const QString defaultPath = SettingsStore::defaultPath();
    check(!defaultPath.isEmpty() && QFileInfo(defaultPath).isAbsolute() &&
          QFileInfo(defaultPath).fileName() == QStringLiteral("settings.json"),
          "default location resolves to an absolute application-local settings path without touching it");
}

void testEquality() {
    using namespace w11widget;
    const PanelSettings defaults;
    check(defaults == PanelSettings{}, "equal settings compare equal");
    check(!defaults.hideForFullscreen, "default settings use the always-visible fullscreen policy");
    auto verifyField = [&defaults](auto mutate) {
        PanelSettings changed;
        mutate(changed);
        check(defaults != changed, "every persisted field participates in equality");
    };
    verifyField([](PanelSettings& s) { s.monitor = QStringLiteral("\\\\.\\DISPLAY1"); });
    verifyField([](PanelSettings& s) { s.side = PanelSide::Right; });
    verifyField([](PanelSettings& s) { ++s.widthDip; });
    verifyField([](PanelSettings& s) { ++s.offsetDip; });
    verifyField([](PanelSettings& s) { s.opacity = 0.5; });
    verifyField([](PanelSettings& s) { s.hideForFullscreen = true; });
    verifyField([](PanelSettings& s) { s.widgets.clear(); });
    verifyField([](PanelSettings& s) {
        s.widgets.push_back({QStringLiteral("equality-fixture-2"), QStringLiteral("sample"), true, true, 120,
                             {{QStringLiteral("label"), QStringLiteral("Eşitlik fikstürü")},
                              {QStringLiteral("accent"), QStringLiteral("#8ddbc2")}}});
        std::swap(s.widgets[0], s.widgets[1]);
    });
    verifyField([](PanelSettings& s) { s.widgets[0].instanceId = QStringLiteral("changed"); });
    verifyField([](PanelSettings& s) { s.widgets[0].typeId = QStringLiteral("future"); });
    verifyField([](PanelSettings& s) { s.widgets[0].enabled = false; });
    verifyField([](PanelSettings& s) { s.widgets[0].visible = false; });
    verifyField([](PanelSettings& s) { ++s.widgets[0].widthDip; });
    verifyField([](PanelSettings& s) { s.widgets[0].settings.insert(QStringLiteral("custom"), 123); });
}

void testCountdownPersistence(const QString& root) {
    using namespace w11widget;
    SettingsStore store(QDir(root).filePath(QStringLiteral("countdown.json")));
    PanelSettings settings;
    const auto countdown = std::find_if(settings.widgets.begin(), settings.widgets.end(),
        [](const auto& widget) { return widget.typeId == QStringLiteral("countdown"); });
    check(countdown != settings.widgets.end(), "default preferences include Countdown with World Clock");
    if (countdown == settings.widgets.end()) return;
    countdown->settings = {
        {QStringLiteral("label"), QStringLiteral("Mesai Bitişi")},
        {QStringLiteral("target_time"), QStringLiteral("19:30")},
        {QStringLiteral("daily_repeat"), false},
        {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")},
        {QStringLiteral("target_date"), QStringLiteral("2026-12-31")}
    };
    QString error;
    check(store.save(settings, error) && store.load().ok && store.load().settings == settings,
          "Countdown target settings survive the atomic save and application restart roundtrip");
}

void testIndependentClockDefaults() {
    using namespace w11widget;
    const auto widgets = defaultWidgetConfigurations();
    check(widgets.size() == 4 && widgets[0].typeId == QStringLiteral("countdown"),
          "defaults put Countdown before three independent clock instances");
    const QStringList zones{QStringLiteral("UTC"), QStringLiteral("America/New_York"),
                            QStringLiteral("Europe/Istanbul")};
    for (int index = 0; index < zones.size(); ++index) {
        const auto& clock = widgets.at(static_cast<std::size_t>(index) + 1);
        check(clock.instanceId == QStringLiteral("world-clock-%1").arg(index + 1) &&
                  clock.typeId == QStringLiteral("world-clock") && clock.widthDip == 72 &&
                  clock.settings.size() == 2 && !clock.settings.value(QStringLiteral("show_date")).toBool() &&
                  clock.settings.value(QStringLiteral("time_zone")) == zones.at(index),
              "each default clock has a stable ID, compact width and exactly one zone");
    }
}

void testGroupedClockMigration(const QString& root) {
    using namespace w11widget;
    SettingsStore store(QDir(root).filePath(QStringLiteral("grouped-clocks.json")));
    const auto clockObject = [](const QString& id, const QJsonObject& settings) {
        return QJsonObject{{QStringLiteral("instance_id"), id},
                           {QStringLiteral("type_id"), QStringLiteral("world-clock")},
                           {QStringLiteral("enabled"), false}, {QStringLiteral("visible"), false},
                           {QStringLiteral("width_dip"), 176}, {QStringLiteral("settings"), settings}};
    };
    const QJsonObject legacySettings{
        {QStringLiteral("show_date"), true},
        {QStringLiteral("time_zones"), QJsonArray{QStringLiteral("UTC"), QStringLiteral("America/New_York"),
                                                 QStringLiteral("Europe/Istanbul")}}
    };
    const auto legacy = clockObject(QStringLiteral("group"), legacySettings);
    const QJsonObject unknown{
        {QStringLiteral("instance_id"), QStringLiteral("group-2")},
        {QStringLiteral("type_id"), QStringLiteral("future-widget")},
        {QStringLiteral("enabled"), true}, {QStringLiteral("visible"), true},
        {QStringLiteral("width_dip"), 120},
        {QStringLiteral("settings"), QJsonObject{{QStringLiteral("time_zones"), QJsonArray{false}}}}
    };
    const auto countdown = validDocument().value(QStringLiteral("widgets")).toArray().first();
    auto document = validDocument();
    document.insert(QStringLiteral("widgets"), QJsonArray{legacy, unknown, countdown});
    const QByteArray original = QJsonDocument(document).toJson(QJsonDocument::Indented);
    check(writeBytes(store.filePath(), original), "legacy grouped clock fixture is created");
    const auto loaded = store.load();
    check(loaded.ok && loaded.settings.widgets.size() == 5 && readBytes(store.filePath()) == original,
          "legacy grouped clocks split in memory without rewriting the original file");
    if (!loaded.ok || loaded.settings.widgets.size() != 5) return;
    const auto& migrated = loaded.settings.widgets;
    check(migrated[0].typeId == QStringLiteral("countdown") &&
              migrated[1].instanceId == QStringLiteral("group") &&
              migrated[2].instanceId == QStringLiteral("group-3") &&
              migrated[3].instanceId == QStringLiteral("group-4") &&
              migrated[4].instanceId == QStringLiteral("group-2"),
          "migration preserves old visual order, first ID and deterministic collision-free split IDs");
    const auto zones = legacySettings.value(QStringLiteral("time_zones")).toArray();
    for (std::size_t index = 1; index <= 3; ++index) {
        const auto& clock = migrated[index];
        check(!clock.enabled && !clock.visible && clock.settings.value(QStringLiteral("show_date")).toBool() &&
                  clock.settings.size() == 2 && clock.settings.value(QStringLiteral("time_zone")) == zones.at(index - 1) &&
                  clock.widthDip >= MinimumWidgetWidthDip && clock.widthDip <= MaximumWidgetWidthDip,
              "each migrated clock preserves date/visibility flags, zone order and a valid individual width");
    }
    check(migrated[4].settings == unknown.value(QStringLiteral("settings")).toObject() &&
              store.load().settings == loaded.settings,
          "unknown widget settings remain untouched and repeated migration is deterministic");
    QString error;
    check(store.save(loaded.settings, error) && store.load().settings == loaded.settings &&
              readBytes(store.filePath()) != original,
          "an explicit save persists independent clocks and a restart does not split them again");
    auto independent = loaded.settings;
    std::swap(independent.widgets[0], independent.widgets[3]);
    check(store.save(independent, error) && store.load().settings == independent,
          "singular clock configurations preserve the user's order including Countdown placement");

    document.insert(QStringLiteral("widgets"), QJsonArray{clockObject(QStringLiteral("old-defaults"),
                                      {{QStringLiteral("show_date"), false}})});
    const auto omittedBytes = QJsonDocument(document).toJson();
    check(writeBytes(store.filePath(), omittedBytes) && store.load().ok &&
              store.load().settings.widgets.size() == 3 && readBytes(store.filePath()) == omittedBytes,
          "legacy omitted time_zones migrates the former three default zones without writing");
    const auto omitted = store.load().settings.widgets;
    check(omitted[0].settings.value(QStringLiteral("time_zone")) == QStringLiteral("UTC") &&
              omitted[1].settings.value(QStringLiteral("time_zone")) == QStringLiteral("America/New_York") &&
              omitted[2].settings.value(QStringLiteral("time_zone")) == QStringLiteral("Europe/Istanbul"),
          "omitted legacy zones retain UTC, New York and Istanbul in their original order");

    document.insert(QStringLiteral("widgets"), QJsonArray{clockObject(QStringLiteral("empty"),
                         {{QStringLiteral("show_date"), true}, {QStringLiteral("time_zones"), QJsonArray{}}}), unknown});
    check(writeBytes(store.filePath(), QJsonDocument(document).toJson()) && store.load().ok &&
              store.load().settings.widgets.size() == 1 &&
              store.load().settings.widgets.front().typeId == QStringLiteral("future-widget"),
          "an explicitly empty legacy clock group creates no unwanted clock instances");

    for (const auto& value : QJsonArray{false, QStringLiteral("UTC"), QJsonArray{42},
                                       QJsonArray{QStringLiteral("Invalid/TimeZone")}, QJsonArray{QString{}}}) {
        document.insert(QStringLiteral("widgets"), QJsonArray{clockObject(QStringLiteral("bad-zones"),
                            {{QStringLiteral("show_date"), false}, {QStringLiteral("time_zones"), value}})});
        expectInvalidDocument(store, QJsonDocument(document).toJson(),
                              "malformed legacy lists and zone values report a non-destructive load error");
    }
    auto ambiguous = legacySettings;
    ambiguous.insert(QStringLiteral("time_zone"), QStringLiteral("UTC"));
    document.insert(QStringLiteral("widgets"), QJsonArray{clockObject(QStringLiteral("ambiguous"), ambiguous)});
    expectInvalidDocument(store, QJsonDocument(document).toJson(), "conflicting singular and legacy zone keys are rejected");

    auto malformedDate = legacySettings;
    malformedDate.insert(QStringLiteral("show_date"), QStringLiteral("invalid"));
    malformedDate.insert(QStringLiteral("future_option"), 42);
    document.insert(QStringLiteral("widgets"), QJsonArray{clockObject(QStringLiteral("runtime-error"), malformedDate)});
    check(writeBytes(store.filePath(), QJsonDocument(document).toJson()) && store.load().ok &&
              store.load().settings.widgets.front().settings.value(QStringLiteral("show_date")) == QStringLiteral("invalid") &&
              store.load().settings.widgets.back().settings.value(QStringLiteral("future_option")) == 42,
          "migration retains other invalid or unknown module settings for the host's per-widget error reporting");

    const QString longId(64, QLatin1Char('a'));
    auto collision = unknown;
    collision.insert(QStringLiteral("instance_id"), longId.left(62) + QStringLiteral("-2"));
    document.insert(QStringLiteral("widgets"), QJsonArray{clockObject(longId, legacySettings), collision});
    check(writeBytes(store.filePath(), QJsonDocument(document).toJson()) && store.load().ok,
          "maximum-length legacy IDs migrate even when truncated suffix IDs collide");
    const auto longIds = store.load().settings.widgets;
    check(longIds.size() == 4 && longIds[0].instanceId == longId &&
              longIds[1].instanceId == longId.left(62) + QStringLiteral("-3") &&
              longIds[2].instanceId == longId.left(62) + QStringLiteral("-4"),
          "split IDs remain at most 64 ASCII characters and preserve the original first ID");

    QJsonArray crowded{legacy};
    for (int index = 0; index < 15; ++index) {
        auto other = unknown;
        other.insert(QStringLiteral("instance_id"), QStringLiteral("other-%1").arg(index));
        crowded.append(other);
    }
    document.insert(QStringLiteral("widgets"), crowded);
    const auto crowdedBytes = QJsonDocument(document).toJson();
    expectInvalidDocument(store, crowdedBytes,
                          "migration that expands beyond sixteen widgets fails without losing saved data");
    const auto crowdedLoad = store.load();
    check(crowdedLoad.error.contains(QStringLiteral("18")) && crowdedLoad.error.contains(QStringLiteral("16")),
          "migration overflow identifies the expanded count and limit explicitly");
    crowded.removeLast();
    crowded.removeLast();
    document.insert(QStringLiteral("widgets"), crowded);
    check(writeBytes(store.filePath(), QJsonDocument(document).toJson()) && store.load().ok &&
              store.load().settings.widgets.size() == MaximumWidgetCount,
          "migration accepts expansion to exactly sixteen instances");

    QJsonArray manyZones;
    for (int index = 0; index < 2000; ++index) manyZones.append(QStringLiteral("UTC"));
    document.insert(QStringLiteral("widgets"), QJsonArray{clockObject(QStringLiteral("many-zones"),
                         {{QStringLiteral("show_date"), false}, {QStringLiteral("time_zones"), manyZones}})});
    const auto manyZoneBytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
    expectInvalidDocument(store, manyZoneBytes,
                          "a small legacy file with thousands of zones fails before constructing split widgets");
    check(store.load().error.contains(QStringLiteral("2000")),
          "large legacy-list overflow reports the complete required instance count");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("w11widget-tests"));
    application.setApplicationName(QStringLiteral("settings-test"));
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "Could not create an isolated temporary test directory.\n";
        return 1;
    }
    testRoundtrip(directory.path());
    testFileSizeLimit(directory.path());
    testLegacyFullscreenMigration(directory.path());
    testInvalidJson(directory.path());
    testWidgetValidation(directory.path());
    testInvalidSave(directory.path());
    testIoFailuresAndLocation(directory.path());
    testEquality();
    testCountdownPersistence(directory.path());
    testIndependentClockDefaults();
    testGroupedClockMigration(directory.path());
    if (failures != 0) {
        std::cerr << failures << " settings checks failed.\n";
        return 1;
    }
    std::cout << "Settings checks passed.\n";
    return 0;
}
