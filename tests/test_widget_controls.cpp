#include "core/WidgetHostModel.h"
#include "ui/WidgetSettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>

#include <algorithm>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

template<class T>
T* control(QWidget& dialog, const char* name)
{
    auto* result = dialog.findChild<T*>(QString::fromLatin1(name));
    check(result != nullptr, name);
    return result;
}

QString selectedId(const QListWidget* list)
{
    return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString{};
}

const w11widget::WidgetConfiguration* findWidget(const w11widget::WidgetHostModel& model,
                                                const QString& id)
{
    const auto& widgets = model.configuration();
    const auto found = std::find_if(widgets.begin(), widgets.end(),
        [&id](const auto& widget) { return widget.instanceId == id; });
    return found == widgets.end() ? nullptr : &*found;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    w11widget::WidgetHostModel model;
    QString error;
    const w11widget::WidgetConfigurations samples{
        {QStringLiteral("sample-1"), QStringLiteral("sample"), true, true, 120,
         {{QStringLiteral("label"), QStringLiteral("Örnek 1")},
          {QStringLiteral("accent"), QStringLiteral("#8ddbc2")}}},
        {QStringLiteral("sample-2"), QStringLiteral("sample"), true, true, 120,
         {{QStringLiteral("label"), QStringLiteral("Örnek 2")},
          {QStringLiteral("accent"), QStringLiteral("#91bfff")}}}
    };
    if (!model.setConfiguration(samples, error)) {
        std::cerr << "FAIL: sample widget fixture: " << error.toStdString() << '\n';
        return 1;
    }
    model.setRunning(true);
    int configurationChanges = 0;
    QObject::connect(&model, &w11widget::WidgetHostModel::configurationChanged,
                     [&configurationChanges] { ++configurationChanges; });

    w11widget::WidgetSettingsDialog dialog(&model);
    check(!dialog.isVisible(), "constructing widget settings keeps the dialog hidden");
    auto* list = control<QListWidget>(dialog, "widgetList");
    auto* add = control<QPushButton>(dialog, "addWidget");
    auto* remove = control<QPushButton>(dialog, "removeWidget");
    auto* up = control<QPushButton>(dialog, "moveWidgetUp");
    auto* down = control<QPushButton>(dialog, "moveWidgetDown");
    auto* enabled = control<QCheckBox>(dialog, "widgetEnabled");
    auto* visible = control<QCheckBox>(dialog, "widgetVisible");
    auto* width = control<QSpinBox>(dialog, "widgetWidth");
    auto* label = control<QLineEdit>(dialog, "widgetLabel");
    auto* accent = control<QComboBox>(dialog, "widgetAccent");
    auto* showDate = control<QCheckBox>(dialog, "worldClockShowDate");
    auto* addWorldClock = control<QPushButton>(dialog, "addWorldClock");
    auto* addCountdown = control<QPushButton>(dialog, "addCountdown");
    auto* zoneEditor = control<QWidget>(dialog, "worldClockZoneEditor");
    auto* timeZone = control<QComboBox>(dialog, "worldClockTimeZone");
    auto* applyTimeZone = control<QPushButton>(dialog, "applyWorldClockTimeZone");
    auto* errorLabel = control<QLabel>(dialog, "widgetError");
    auto* emptyHint = control<QLabel>(dialog, "widgetEmptyHint");
    if (failures != 0) return 1;

    check(list->count() == 2, "all configured widgets appear in settings");
    check(configurationChanges == 0, "initial controls do not rewrite configuration");
    const auto firstId = model.configuration()[0].instanceId;
    const auto secondId = model.configuration()[1].instanceId;
    list->setCurrentRow(1);
    check(selectedId(list) == secondId, "select second instance by identity");
    check(configurationChanges == 0, "selecting a widget does not save unchanged fields");

    enabled->click();
    check(!findWidget(model, secondId)->enabled, "enabled checkbox edits only selected widget");
    check(findWidget(model, firstId)->enabled, "other instance remains enabled");
    check(selectedId(list) == secondId, "selection survives enabled-state update");
    enabled->click();
    visible->click();
    check(!findWidget(model, secondId)->visible, "visible checkbox edits selected widget");
    visible->click();

    width->setValue(156);
    check(findWidget(model, secondId)->widthDip == 156, "width control edits selected widget");
    label->setText(QStringLiteral("Benim örneğim"));
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    check(findWidget(model, secondId)->settings.value(QStringLiteral("label")).toString() ==
              QStringLiteral("Benim örneğim"), "editing label applies selected widget settings");
    const auto accentIndex = accent->findData(QStringLiteral("#ffd48a"));
    check(accentIndex >= 0, "accent editor provides the supported color");
    accent->setCurrentIndex(accentIndex);
    check(findWidget(model, secondId)->settings.value(QStringLiteral("accent")).toString() ==
              QStringLiteral("#ffd48a"), "accent editor applies selected widget settings");
    check(findWidget(model, secondId)->settings.value(QStringLiteral("label")).toString() ==
              QStringLiteral("Benim örneğim"), "accent edit preserves existing label setting");

    const int beforeReselect = configurationChanges;
    list->setCurrentRow(0);
    list->setCurrentRow(1);
    check(configurationChanges == beforeReselect, "editor refresh signals do not create recursive writes");
    up->click();
    check(model.configuration()[0].instanceId == secondId, "up moves selected widget to previous position");
    check(selectedId(list) == secondId && list->currentRow() == 0,
          "selection follows instance identity after moving up");
    down->click();
    check(model.configuration()[1].instanceId == secondId && selectedId(list) == secondId,
          "down moves widget while preserving selection");

    add->click();
    check(model.configuration().size() == 3 && list->count() == 3,
          "add button creates an independent configured widget");
    const auto addedId = selectedId(list);
    check(!addedId.isEmpty() && addedId != firstId && addedId != secondId,
          "new widget receives its own identity and becomes selected");
    remove->click();
    check(model.configuration().size() == 2 && findWidget(model, addedId) == nullptr,
          "remove button removes selected widget only");
    check(errorLabel->text().isEmpty(), "successful edits leave no error text");

    model.setRunning(false);
    check(!dialog.isVisible(), "lifecycle updates keep settings dialog hidden");
    while (list->count() > 0) remove->click();
    check(model.configuration().empty(), "removing every widget is supported");
    check(!emptyHint->isHidden(), "empty configuration exposes an explanatory hint");
    check(!enabled->isEnabled() && !label->isEnabled() && !remove->isEnabled(),
          "empty configuration disables instance-only controls");
    add->click();
    check(model.configuration().size() == 1 && list->count() == 1,
          "an empty configuration can add a new sample");
    enabled->click();
    check(!emptyHint->isHidden(), "all disabled modules expose the empty-panel hint");

    const w11widget::WidgetConfigurations clocks{
        {QStringLiteral("clock-utc"), QStringLiteral("world-clock"), true, true, 72,
         {{QStringLiteral("show_date"), false}, {QStringLiteral("time_zone"), QStringLiteral("UTC")}}},
        {QStringLiteral("clock-ny"), QStringLiteral("world-clock"), true, true, 72,
         {{QStringLiteral("show_date"), false},
          {QStringLiteral("time_zone"), QStringLiteral("America/New_York")}}},
        {QStringLiteral("clock-istanbul"), QStringLiteral("world-clock"), true, true, 72,
         {{QStringLiteral("show_date"), false},
          {QStringLiteral("time_zone"), QStringLiteral("Europe/Istanbul")}}}
    };
    check(model.setConfiguration(clocks, error), "independent clock fixture configuration is accepted");
    check(list->count() == 3 && list->item(0)->text().contains(QStringLiteral("UTC")) &&
              list->item(1)->text().contains(QStringLiteral("America/New_York")) &&
              list->item(2)->text().contains(QStringLiteral("Europe/Istanbul")),
          "each independent clock is distinguishable by its time zone in the widget list");
    list->setCurrentRow(1);
    check(showDate->isEnabled() && !showDate->isChecked(),
          "selected clock exposes its own date-display setting");
    check(!zoneEditor->isHidden() && timeZone->currentText() == QStringLiteral("America/New_York"),
          "selected clock exposes one editable IANA time zone");
    check(!width->isEnabled(), "content-sized clocks do not expose an ineffective preferred-width edit");
    check(label->isEnabled() && label->text() == QStringLiteral("New York") && !accent->isEnabled() &&
              !label->placeholderText().isEmpty() && !label->toolTip().isEmpty(),
          "clock label is prefilled with its editable default name while accent remains sample-only");
    const int beforeDefaultLabel = configurationChanges;
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    list->setCurrentRow(2);
    check(label->text() == QStringLiteral("İstanbul"),
          "Istanbul clock starts with its editable Turkish default name");
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    list->setCurrentRow(1);
    check(model.configuration() == clocks && configurationChanges == beforeDefaultLabel,
          "leaving or reselecting untouched default clock names does not write custom labels");
    const int beforeZoneTyping = configurationChanges;
    timeZone->setEditText(QStringLiteral("Asia/Tokyo"));
    QMetaObject::invokeMethod(timeZone->lineEdit(), "editingFinished", Qt::DirectConnection);
    check(model.configuration() == clocks && configurationChanges == beforeZoneTyping,
          "typing or leaving a time zone does not persist before Apply");
    label->setText(QStringLiteral("  Tokyo ofisi  "));
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    check(timeZone->currentText() == QStringLiteral("Asia/Tokyo") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("time_zone")).toString() ==
                  QStringLiteral("America/New_York") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString() ==
                  QStringLiteral("Tokyo ofisi") &&
              *findWidget(model, clocks[0].instanceId) == clocks[0] &&
              *findWidget(model, clocks[2].instanceId) == clocks[2],
          "clock label autosave preserves a pending time-zone edit without applying it or changing other clocks");
    applyTimeZone->click();
    check(findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("time_zone")).toString() ==
              QStringLiteral("Asia/Tokyo") && selectedId(list) == clocks[1].instanceId &&
              label->text() == QStringLiteral("Tokyo ofisi") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString() ==
                  QStringLiteral("Tokyo ofisi"),
          "Apply saves the pending time zone after label autosave and preserves the new clock name");
    check(*findWidget(model, clocks[0].instanceId) == clocks[0] &&
              *findWidget(model, clocks[2].instanceId) == clocks[2],
          "changing one clock preserves every setting of the other clocks");
    check(list->currentItem()->text().contains(QStringLiteral("Asia/Tokyo")),
          "clock list label follows the selected clock's new time zone");

    label->setText(QStringLiteral("  Tokyo ofisi  "));
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    check(findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString() ==
              QStringLiteral("Tokyo ofisi") && label->text() == QStringLiteral("Tokyo ofisi") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("time_zone")).toString() ==
                  QStringLiteral("Asia/Tokyo") &&
              !findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("show_date")).toBool(),
          "finishing a clock label saves its trimmed name and preserves the selected zone/date settings");
    check(*findWidget(model, clocks[0].instanceId) == clocks[0] &&
              *findWidget(model, clocks[2].instanceId) == clocks[2],
          "renaming one clock preserves every setting of the other clocks");
    check(list->currentItem()->text().startsWith(QStringLiteral("Tokyo ofisi · Asia/Tokyo")),
          "clock list prominently shows the custom name while retaining its time zone");
    const int beforeLabelReselect = configurationChanges;
    list->setCurrentRow(0);
    check(label->isEnabled() && label->text() == QStringLiteral("UTC"),
          "selecting another clock displays that clock's independent default label");
    list->setCurrentRow(1);
    check(label->text() == QStringLiteral("Tokyo ofisi") && configurationChanges == beforeLabelReselect,
          "reselecting a named clock restores its label without writing settings");

    const auto beforeInvalidZone = model.configuration();
    const int changesBeforeInvalidZone = configurationChanges;
    timeZone->setEditText(QStringLiteral("Invalid/Not_A_Zone"));
    applyTimeZone->click();
    check(model.configuration() == beforeInvalidZone && configurationChanges == changesBeforeInvalidZone,
          "invalid time zone is rejected without changing or saving any clock");
    check(!errorLabel->text().isEmpty() && timeZone->currentText() == QStringLiteral("Invalid/Not_A_Zone"),
          "invalid time zone leaves a visible error and editable input for correction");
    const QString emptyZones[] = {QString{}, QStringLiteral(" \t ")};
    for (const auto& emptyZone : emptyZones) {
        timeZone->setEditText(emptyZone);
        applyTimeZone->click();
        check(model.configuration() == beforeInvalidZone && configurationChanges == changesBeforeInvalidZone &&
                  !errorLabel->text().isEmpty(),
              "empty or whitespace-only time zone shows an error without changing or saving any clock");
    }
    timeZone->setEditText(QStringLiteral("Europe/London"));
    applyTimeZone->click();
    check(errorLabel->text().isEmpty() &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("time_zone")).toString() ==
                  QStringLiteral("Europe/London") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString() ==
                  QStringLiteral("Tokyo ofisi"),
          "correcting a rejected zone applies successfully, preserves the clock label and clears the error");
    showDate->click();
    check(findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("show_date")).toBool() &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("time_zone")).toString() ==
                  QStringLiteral("Europe/London") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString() ==
                  QStringLiteral("Tokyo ofisi") &&
              *findWidget(model, clocks[0].instanceId) == clocks[0] &&
              *findWidget(model, clocks[2].instanceId) == clocks[2],
          "date toggle edits only the selected clock and preserves its name and all time zones");
    label->clear();
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    check(findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString().isEmpty() &&
              label->text() == QStringLiteral("London") &&
              !list->currentItem()->text().contains(QStringLiteral("Tokyo ofisi")) &&
              list->currentItem()->text().contains(QStringLiteral("Europe/London")) &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("show_date")).toBool() &&
              *findWidget(model, clocks[0].instanceId) == clocks[0] &&
              *findWidget(model, clocks[2].instanceId) == clocks[2],
          "clearing a clock name restores the editable default city name and preserves date, zone and other clocks");
    const int beforeBlankLabel = configurationChanges;
    label->setText(QStringLiteral(" \t "));
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    check(label->text() == QStringLiteral("London") && configurationChanges == beforeBlankLabel,
          "whitespace-only clock name restores the default name without saving unchanged settings");
    QMetaObject::invokeMethod(label, "editingFinished", Qt::DirectConnection);
    check(configurationChanges == beforeBlankLabel &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString().isEmpty(),
          "leaving a default name after clearing retains the empty stored label without saving");
    timeZone->setEditText(QStringLiteral("Asia/Tokyo"));
    applyTimeZone->click();
    check(label->text() == QStringLiteral("Tokyo") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("time_zone")).toString() ==
                  QStringLiteral("Asia/Tokyo") &&
              findWidget(model, clocks[1].instanceId)->settings.value(QStringLiteral("label")).toString().isEmpty(),
          "changing a clock zone without a custom label follows the new default city name");
    visible->click();
    check(!findWidget(model, clocks[1].instanceId)->visible &&
              *findWidget(model, clocks[0].instanceId) == clocks[0] &&
              *findWidget(model, clocks[2].instanceId) == clocks[2],
          "hiding one clock preserves the other clocks and their visibility");
    enabled->click();
    check(!findWidget(model, clocks[1].instanceId)->enabled &&
              *findWidget(model, clocks[0].instanceId) == clocks[0] &&
              *findWidget(model, clocks[2].instanceId) == clocks[2],
          "disabling one clock preserves the other clocks and their enabled state");
    const auto editedClock = *findWidget(model, clocks[1].instanceId);
    up->click();
    check(model.configuration()[0] == editedClock && model.configuration()[1] == clocks[0] &&
              model.configuration()[2] == clocks[2] && selectedId(list) == editedClock.instanceId,
          "reordering one clock preserves all identities and individual settings");
    down->click();
    remove->click();
    check(model.configuration() == w11widget::WidgetConfigurations{clocks[0], clocks[2]},
          "removing one clock leaves the other clocks unchanged");

    const auto beforeAddingClock = model.configuration();
    addWorldClock->click();
    check(model.configuration().size() == beforeAddingClock.size() + 1 && list->count() == 3 &&
              model.configuration()[0] == beforeAddingClock[0] &&
              model.configuration()[1] == beforeAddingClock[1],
          "Saat ekle adds exactly one independent clock and preserves existing clocks");
    const auto& addedClock = model.configuration().back();
    check(addedClock.typeId == QStringLiteral("world-clock") &&
              addedClock.settings.value(QStringLiteral("time_zone")).toString() == QStringLiteral("UTC") &&
              !addedClock.settings.contains(QStringLiteral("time_zones")) &&
              selectedId(list) == addedClock.instanceId && !showDate->isChecked(),
          "new clock has one UTC zone, its own identity and independent date preference");
    addCountdown->click();
    check(model.configuration().size() == 4 &&
              model.configuration().back().typeId == QStringLiteral("countdown"),
          "the manager adds an independent Countdown beside the separate clocks");
    check(!width->isEnabled() && zoneEditor->isHidden(),
          "Countdown also sizes from content and hides the clock-specific zone editor");

    if (failures != 0) return 1;
    std::cout << "PASS: hidden widget settings controls and model integration\n";
    return 0;
}
