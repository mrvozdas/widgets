#include "core/IWidget.h"
#include "core/WidgetHostModel.h"
#include "core/WidgetRegistry.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

#include <iostream>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace {

int failures = 0;

void check(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

class LifecycleWidget final : public QObject, public w11widget::IWidget {
public:
    static inline int initialized = 0;
    static inline int updated = 0;
    static inline int activated = 0;
    static inline int suspended = 0;
    static inline int shutDown = 0;

    static void reset()
    {
        initialized = 0;
        updated = 0;
        activated = 0;
        suspended = 0;
        shutDown = 0;
    }

    bool validateSettings(const QJsonObject& settings, QString& error) const override
    {
        error.clear();
        if (settings.value(QStringLiteral("valid")).toBool()) return true;
        error = QStringLiteral("The fixture rejected its settings.");
        return false;
    }

    bool initialize(const QJsonObject&, QString& error) override
    {
        error.clear();
        ++initialized;
        return true;
    }

    bool updateSettings(const QJsonObject&, QString& error) override
    {
        error.clear();
        ++updated;
        return true;
    }

    bool activate(QString& error) override
    {
        error.clear();
        ++activated;
        return true;
    }

    bool suspend(QString& error) override
    {
        error.clear();
        ++suspended;
        return true;
    }

    void shutdown() override { ++shutDown; }
    QObject* viewModel() override { return this; }
};

w11widget::WidgetDescriptor descriptor(const QString& typeId = QStringLiteral("fixture"))
{
    return {typeId, QStringLiteral("Fixture"), w11widget::WidgetApiVersion,
            QUrl(QStringLiteral("qrc:/qml/SampleWidget.qml")), {}};
}

void testRegistry()
{
    using namespace w11widget;
    WidgetRegistry registry;
    QString error;

    auto invalidApi = descriptor();
    invalidApi.apiVersion = WidgetApiVersion + 1;
    check(!registry.registerWidget(invalidApi, [] { return std::make_unique<LifecycleWidget>(); }, error) &&
              error.contains(QStringLiteral("API version")),
          "incompatible API versions are rejected");

    auto externalResource = descriptor();
    externalResource.qmlSource = QUrl(QStringLiteral("file:///C:/widget.qml"));
    check(!registry.registerWidget(externalResource, [] { return std::make_unique<LifecycleWidget>(); }, error) &&
              error.contains(QStringLiteral("qrc")),
          "non-local QML resources are rejected");

    auto traversalResource = descriptor();
    traversalResource.qmlSource = QUrl(QStringLiteral("qrc:/qml/../widget.qml"));
    check(!registry.registerWidget(traversalResource, [] { return std::make_unique<LifecycleWidget>(); }, error) &&
              error.contains(QStringLiteral("qrc")),
          "resource paths with traversal are rejected");

    check(registry.registerWidget(descriptor(), [] { return std::make_unique<LifecycleWidget>(); }, error) &&
              error.isEmpty(),
          "a compatible built-in widget registers");
    check(!registry.registerWidget(descriptor(), [] { return std::make_unique<LifecycleWidget>(); }, error) &&
              error.contains(QStringLiteral("already registered")),
          "duplicate type registration is rejected");
    check(registry.create(QStringLiteral("missing"), error) == nullptr && !error.isEmpty(),
          "unregistered widgets produce an explicit creation error");
}

void testLifecycleAndErrors()
{
    using namespace w11widget;
    LifecycleWidget::reset();
    WidgetRegistry registry;
    QString error;
    check(registry.registerWidget(descriptor(), [] { return std::make_unique<LifecycleWidget>(); }, error),
          "lifecycle fixture registers");
    WidgetHostModel host(std::move(registry));

    const WidgetConfiguration valid{QStringLiteral("fixture-1"), QStringLiteral("fixture"), true, true, 120,
                                    {{QStringLiteral("valid"), true}}};
    check(host.setConfiguration({valid}, error) && error.isEmpty(),
          "a valid widget configuration initializes its module");
    check(LifecycleWidget::initialized == 1 && LifecycleWidget::updated == 0,
          "initialization occurs exactly once before running");
    host.setRunning(true);
    check(LifecycleWidget::activated == 1, "running host activates visible enabled modules");

    auto updated = valid;
    updated.settings = {{QStringLiteral("valid"), true}, {QStringLiteral("revision"), 1}};
    check(host.setWidgetSettings(updated.instanceId, updated.settings, error),
          "settings updates are accepted by a running module");
    check(LifecycleWidget::updated == 1, "settings updates reach the initialized module");

    check(host.setVisible(valid.instanceId, false, error), "visible state can be disabled");
    check(LifecycleWidget::suspended == 1, "hiding a running module suspends it");
    check(host.setVisible(valid.instanceId, true, error), "visible state can be restored");
    check(LifecycleWidget::activated == 2, "restoring visibility reactivates the module");

    WidgetConfiguration unknown{QStringLiteral("future-1"), QStringLiteral("future.widget"), true, true, 120, {}};
    check(host.setConfiguration({unknown}, error), "unknown persisted type IDs remain structurally valid");
    const auto unknownIndex = host.index(0);
    check(host.data(unknownIndex, WidgetHostModel::LifecycleStateRole).toString() == QStringLiteral("error") &&
              !host.data(unknownIndex, WidgetHostModel::ErrorMessageRole).toString().isEmpty(),
          "unknown module failures are contained in an error entry");
    check(host.setConfiguration({}, error), "error entries can be removed");
    check(LifecycleWidget::shutDown >= 1, "removed initialized modules shut down");

    WidgetRegistry throwingRegistry;
    check(throwingRegistry.registerWidget(descriptor(QStringLiteral("throwing")),
                                         []() -> std::unique_ptr<IWidget> {
                                             throw std::runtime_error("fixture failure");
                                         }, error),
          "throwing fixture registers");
    WidgetHostModel throwingHost(std::move(throwingRegistry));
    WidgetConfiguration throwing{QStringLiteral("throwing-1"), QStringLiteral("throwing"), true, true, 120, {}};
    check(throwingHost.setConfiguration({throwing}, error), "factory failures do not reject valid persisted state");
    check(throwingHost.data(throwingHost.index(0), WidgetHostModel::LifecycleStateRole).toString() ==
              QStringLiteral("error"),
          "factory exceptions are contained in an error entry");
}

void testBuiltInCountdownSettings()
{
    using namespace w11widget;
    WidgetHostModel host;
    const auto& configuration = host.configuration();
    const auto countdown = std::find_if(configuration.begin(), configuration.end(),
        [](const auto& item) { return item.typeId == QStringLiteral("countdown"); });
    check(countdown != configuration.end(), "built-in host includes Countdown beside independent clocks");
    if (countdown == configuration.end()) return;

    auto settings = countdown->settings.toVariantMap();
    settings.insert(QStringLiteral("label"), QStringLiteral("Mesai Bitişi"));
    settings.insert(QStringLiteral("target_time"), QStringLiteral("19:30"));
    settings.insert(QStringLiteral("daily_repeat"), false);
    settings.insert(QStringLiteral("target_date"), QStringLiteral("2026-12-31"));
    check(host.updateWidgetSettings(countdown->instanceId, settings).isEmpty(),
          "Countdown popup settings update its persisted host configuration");
    const auto updated = std::find_if(host.configuration().begin(), host.configuration().end(),
        [](const auto& item) { return item.typeId == QStringLiteral("countdown"); });
    check(updated != host.configuration().end() &&
              updated->settings.value(QStringLiteral("target_time")).toString() == QStringLiteral("19:30") &&
              !updated->settings.value(QStringLiteral("daily_repeat")).toBool() &&
              updated->settings.value(QStringLiteral("target_date")).toString() == QStringLiteral("2026-12-31"),
          "Countdown configuration retains target, repeat and one-shot date");
}

void testBuiltInIndependentClocks()
{
    using namespace w11widget;
    WidgetHostModel host;
    const auto modelFor = [&](const QString& id) -> QObject* {
        for (int row = 0; row < host.rowCount(); ++row) {
            if (host.data(host.index(row), WidgetHostModel::InstanceIdRole).toString() == id)
                return host.data(host.index(row), WidgetHostModel::ViewModelRole).value<QObject*>();
        }
        return nullptr;
    };
    const auto configuration = host.configuration();
    check(configuration.size() == 4 && configuration.front().typeId == QStringLiteral("countdown"),
          "built-in defaults contain Countdown followed by three separate clocks");
    if (configuration.size() != 4) return;
    const QStringList zones{QStringLiteral("UTC"), QStringLiteral("America/New_York"),
                            QStringLiteral("Europe/Istanbul")};
    std::vector<QPointer<QObject>> models;
    for (int index = 0; index < zones.size(); ++index) {
        const auto& item = configuration[static_cast<std::size_t>(index + 1)];
        QObject* model = modelFor(item.instanceId);
        models.push_back(model);
        const QVariantList readings = model ? model->property("clocks").toList() : QVariantList{};
        check(item.instanceId == QStringLiteral("world-clock-%1").arg(index + 1) &&
                  item.typeId == QStringLiteral("world-clock") &&
                  item.settings.value(QStringLiteral("time_zone")).toString() == zones[index] &&
                  !item.settings.contains(QStringLiteral("time_zones")) && readings.size() == 1 &&
                  readings.front().toMap().value(QStringLiteral("zoneId")).toString() == zones[index],
              "each default clock stores and renders its own singular timezone");
    }
    if (!models[0] || !models[1] || !models[2]) return;
    check(models[0] != models[1] && models[1] != models[2] && models[0] != models[2],
          "the three timezone clocks have independent module instances");
    host.setRunning(true);
    QString error;
    check(host.setWidgetSettings(QStringLiteral("world-clock-1"),
              {{QStringLiteral("time_zone"), QStringLiteral("Asia/Tokyo")},
               {QStringLiteral("show_date"), true}}, error),
          "a running clock accepts its own timezone and date settings");
    const auto readings = models[0]->property("clocks").toList();
    check(readings.size() == 1 &&
              readings.front().toMap().value(QStringLiteral("zoneId")).toString() == QStringLiteral("Asia/Tokyo") &&
              models[0]->property("showDate").toBool() && !models[1]->property("showDate").toBool() &&
              !models[2]->property("showDate").toBool() && host.configuration()[2] == configuration[2] &&
              host.configuration()[3] == configuration[3],
          "editing one clock leaves the other zones and date settings unchanged");
    const auto checkOtherClocks = [&] {
        check(modelFor(QStringLiteral("world-clock-2")) == models[1] &&
                  modelFor(QStringLiteral("world-clock-3")) == models[2] &&
                  models[1]->property("active").toBool() && models[2]->property("active").toBool(),
              "changing one clock preserves the remaining active clock models");
    };
    check(host.setVisible(QStringLiteral("world-clock-1"), false, error) &&
              !models[0]->property("active").toBool(),
          "hiding one clock suspends only its lifecycle");
    checkOtherClocks();
    check(host.setVisible(QStringLiteral("world-clock-1"), true, error) &&
              host.moveWidget(QStringLiteral("world-clock-1"), 0, error) &&
              modelFor(QStringLiteral("world-clock-1")) == models[0] && models[0]->property("active").toBool(),
          "showing and reordering one clock keeps the same live module");
    checkOtherClocks();
    check(host.setEnabled(QStringLiteral("world-clock-1"), false, error) && models[0].isNull(),
          "disabling a single clock destroys its module");
    checkOtherClocks();
    check(host.setEnabled(QStringLiteral("world-clock-1"), true, error),
          "one disabled clock can be restored");
    QPointer<QObject> restored = modelFor(QStringLiteral("world-clock-1"));
    check(restored && restored->property("showDate").toBool() && restored->property("active").toBool(),
          "restored clock retains its independent settings");
    check(host.removeWidget(QStringLiteral("world-clock-1"), error) && restored.isNull(),
          "removing one clock shuts down its module");
    checkOtherClocks();

    check(host.addWidget(QStringLiteral("world-clock"), error), "the manager can add another individual clock");
    const auto& added = host.configuration().back();
    QObject* addedModel = modelFor(added.instanceId);
    const auto addedReadings = addedModel ? addedModel->property("clocks").toList() : QVariantList{};
    check(added.typeId == QStringLiteral("world-clock") &&
              added.settings.value(QStringLiteral("time_zone")).toString() == QStringLiteral("UTC") &&
              addedReadings.size() == 1 &&
              addedReadings.front().toMap().value(QStringLiteral("zoneId")).toString() == QStringLiteral("UTC"),
          "an added clock starts as one independent UTC clock");
    checkOtherClocks();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    testRegistry();
    testLifecycleAndErrors();
    testBuiltInCountdownSettings();
    testBuiltInIndependentClocks();
    return failures == 0 ? 0 : 1;
}
