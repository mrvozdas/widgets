#include "ui/Strings.h"

#include "ui/WidgetSettingsDialog.h"
#include "ui/FluentStyle.h"

#include "core/WidgetHostModel.h"
#include "widgets/WorldClockWidget.h"

#include <QCheckBox>
#include <QAction>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>

namespace w11widget {
namespace {

QVariant roleValue(const WidgetHostModel* model, int row, const QByteArray& name)
{
    const auto roles = model->roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (it.value() == name) return model->data(model->index(row), it.key());
    }
    return {};
}

QString worldClockTimeZone(const QJsonObject& settings)
{
    return settings.value(QStringLiteral("time_zone")).toString(QStringLiteral("UTC"));
}

QString worldClockLabel(const QJsonObject& settings)
{
    const auto customLabel = settings.value(QStringLiteral("label")).toString().trimmed();
    return customLabel.isEmpty() ?
        WorldClockWidget::defaultLabelForTimeZone(worldClockTimeZone(settings)) : customLabel;
}

} // namespace

WidgetSettingsDialog::WidgetSettingsDialog(WidgetHostModel* model, QWidget* parent)
    : QDialog(parent), model_(model)
{
    setObjectName(QStringLiteral("widgetSettingsDialog"));
    setWindowTitle(uiText("ui.w11widget_widgetlar"));
    setModal(false);
    setMinimumSize(620, 440);
    if (!parent) applyFluentStyle(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(uiText("ui.widgetlar"), this);
    title->setProperty("heading", true);
    heading->addWidget(title, 1);
    auto* back = new QPushButton(uiText("ui.yerlesime_don"), this);
    back->setObjectName(QStringLiteral("widgetSettingsBack"));
    heading->addWidget(back);
    layout->addLayout(heading);
    auto* explanation = new QLabel(QStringLiteral(
        "İhtiyacın olan widget’ları ekle ve paneldeki sırasını düzenle."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(16);
    auto* listColumn = new QVBoxLayout;
    add_ = new QPushButton(uiText("ui.widget_ekle"), this);
    add_->setObjectName(QStringLiteral("addWidget"));
    add_->setProperty("primary", true);
    auto* catalog = new QMenu(add_);
    catalog->setObjectName(QStringLiteral("widgetCatalog"));
    add_->setMenu(catalog);
    if (model_) {
        QMenu* development = nullptr;
        for (const auto& descriptor : model_->registry().descriptors()) {
            auto* destination = catalog;
            if (descriptor.typeId == QStringLiteral("sample")) {
                if (!development) development = catalog->addMenu(uiText("ui.gelistirme_ornekleri"));
                destination = development;
            }
            auto* action = destination->addAction(descriptor.displayName);
            action->setData(descriptor.typeId);
            action->setObjectName(descriptor.typeId == QStringLiteral("world-clock")
                ? QStringLiteral("addWorldClock") : descriptor.typeId == QStringLiteral("countdown")
                ? QStringLiteral("addCountdown") : QStringLiteral("add-") + descriptor.typeId);
            connect(action, &QAction::triggered, this, [this, typeId = descriptor.typeId] {
                const auto previous = model_->configuration().size();
                applyChange([this, &typeId](QString& error) { return model_->addWidget(typeId, error); });
                const auto& current = model_->configuration();
                if (current.size() > previous) refreshList(current.back().instanceId);
            });
        }
        if (development) {
            catalog->removeAction(development->menuAction());
            catalog->addSeparator();
            catalog->addMenu(development);
        }
    }
    listColumn->addWidget(add_);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("widgetList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setMinimumWidth(220);
    list_->setWordWrap(true);
    listColumn->addWidget(list_, 1);
    auto* order = new QHBoxLayout;
    up_ = new QPushButton(uiText("ui.yukari"), this);
    up_->setObjectName(QStringLiteral("moveWidgetUp"));
    down_ = new QPushButton(uiText("ui.asagi"), this);
    down_->setObjectName(QStringLiteral("moveWidgetDown"));
    order->addWidget(up_);
    order->addWidget(down_);
    listColumn->addLayout(order);
    columns->addLayout(listColumn, 4);

    auto* inspector = new QFrame(this);
    inspector->setProperty("card", true);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(16, 16, 16, 16);
    inspectorLayout->setSpacing(12);
    selectionTitle_ = new QLabel(inspector);
    selectionTitle_->setObjectName(QStringLiteral("widgetSelectionTitle"));
    selectionTitle_->setProperty("section", true);
    selectionTitle_->setWordWrap(true);
    inspectorLayout->addWidget(selectionTitle_);
    auto* scroll = new QScrollArea(inspector);
    scroll->setWidgetResizable(true);
    auto* fields = new QWidget(scroll);
    fields->setObjectName(QStringLiteral("widgetFields"));
    scroll->setWidget(fields);
    scroll->viewport()->setAutoFillBackground(false);
    auto* fieldsLayout = new QVBoxLayout(fields);
    fieldsLayout->setContentsMargins(0, 0, 0, 0);
    fieldsLayout->setSpacing(12);
    detailsForm_ = new QFormLayout;
    detailsForm_->setSpacing(8);
    detailsForm_->setRowWrapPolicy(QFormLayout::WrapAllRows);
    detailsForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    enabled_ = new QCheckBox(uiText("ui.widget_etkin"), fields);
    enabled_->setObjectName(QStringLiteral("widgetEnabled"));
    visible_ = new QCheckBox(uiText("ui.panelde_goster"), fields);
    visible_->setObjectName(QStringLiteral("widgetVisible"));
    width_ = new QSpinBox(fields);
    width_->setObjectName(QStringLiteral("widgetWidth"));
    width_->setRange(MinimumWidgetWidthDip, MaximumWidgetWidthDip);
    width_->setSuffix(QStringLiteral(" px"));
    label_ = new QLineEdit(fields);
    label_->setObjectName(QStringLiteral("widgetLabel"));
    label_->setMaxLength(40);
    label_->setClearButtonEnabled(true);
    accent_ = new QComboBox(fields);
    accent_->setObjectName(QStringLiteral("widgetAccent"));
    accent_->addItem(QStringLiteral("Nane"), QStringLiteral("#8ddbc2"));
    accent_->addItem(uiText("ui.mavi"), QStringLiteral("#91bfff"));
    accent_->addItem(uiText("ui.sari"), QStringLiteral("#ffd48a"));
    accent_->addItem(uiText("ui.pembe"), QStringLiteral("#f3a8cf"));
    showDate_ = new QCheckBox(uiText("ui.tarihi_goster"), fields);
    showDate_->setObjectName(QStringLiteral("worldClockShowDate"));
    detailsForm_->addRow(enabled_);
    detailsForm_->addRow(visible_);
    detailsForm_->addRow(uiText("ui.genislik_2"), width_);
    detailsForm_->addRow(uiText("ui.gorunen_ad"), label_);
    detailsForm_->addRow(uiText("ui.vurgu_rengi"), accent_);
    detailsForm_->addRow(showDate_);
    fieldsLayout->addLayout(detailsForm_);

    worldClockZoneEditor_ = new QWidget(fields);
    worldClockZoneEditor_->setObjectName(QStringLiteral("worldClockZoneEditor"));
    auto* zoneLayout = new QVBoxLayout(worldClockZoneEditor_);
    zoneLayout->setContentsMargins(0, 0, 0, 0);
    zoneLayout->addWidget(new QLabel(uiText("ui.saat_dilimi"), worldClockZoneEditor_));
    timeZone_ = new QComboBox(worldClockZoneEditor_);
    timeZone_->setObjectName(QStringLiteral("worldClockTimeZone"));
    timeZone_->setEditable(true);
    timeZone_->setInsertPolicy(QComboBox::NoInsert);
    timeZone_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    timeZone_->setMinimumContentsLength(12);
    QList<QByteArray> availableZones = QTimeZone::availableTimeZoneIds();
    std::sort(availableZones.begin(), availableZones.end());
    for (const auto& zone : availableZones) timeZone_->addItem(QString::fromLatin1(zone), zone);
    applyTimeZone_ = new QPushButton(uiText("ui.saat_dilimini_uygula"), worldClockZoneEditor_);
    applyTimeZone_->setObjectName(QStringLiteral("applyWorldClockTimeZone"));
    applyTimeZone_->setAutoDefault(false);
    zoneLayout->addWidget(timeZone_);
    zoneLayout->addWidget(applyTimeZone_);
    fieldsLayout->addWidget(worldClockZoneEditor_);
    detailsHint_ = new QLabel(fields);
    detailsHint_->setWordWrap(true);
    fieldsLayout->addWidget(detailsHint_);
    error_ = new QLabel(fields);
    error_->setObjectName(QStringLiteral("widgetError"));
    error_->setWordWrap(true);
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fieldsLayout->addWidget(error_);
    fieldsLayout->addStretch();
    inspectorLayout->addWidget(scroll, 1);
    remove_ = new QPushButton(uiText("ui.widgeti_kaldir"), inspector);
    remove_->setObjectName(QStringLiteral("removeWidget"));
    inspectorLayout->addWidget(remove_);
    columns->addWidget(inspector, 5);
    layout->addLayout(columns, 1);
    emptyHint_ = new QLabel(this);
    emptyHint_->setObjectName(QStringLiteral("widgetEmptyHint"));
    emptyHint_->setWordWrap(true);
    layout->addWidget(emptyHint_);
    connect(back, &QPushButton::clicked, this, &WidgetSettingsDialog::backRequested);
    connect(list_, &QListWidget::currentRowChanged, this, [this] {
        if (!refreshing_) refreshEditor();
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const auto id = selectedId();
        applyChange([this, id](QString& error) { return model_->removeWidget(id, error); });
    });
    connect(up_, &QPushButton::clicked, this, [this] {
        const auto id = selectedId();
        const auto target = list_->currentRow() - 1;
        applyChange([this, id, target](QString& error) { return model_->moveWidget(id, target, error); }, id);
    });
    connect(down_, &QPushButton::clicked, this, [this] {
        const auto id = selectedId();
        const auto target = list_->currentRow() + 1;
        applyChange([this, id, target](QString& error) { return model_->moveWidget(id, target, error); }, id);
    });
    connect(enabled_, &QCheckBox::toggled, this, [this](bool checked) {
        const auto id = selectedId();
        applyChange([this, id, checked](QString& error) { return model_->setEnabled(id, checked, error); }, id);
    });
    connect(visible_, &QCheckBox::toggled, this, [this](bool checked) {
        const auto id = selectedId();
        applyChange([this, id, checked](QString& error) { return model_->setVisible(id, checked, error); }, id);
    });
    connect(width_, &QSpinBox::valueChanged, this, [this](int width) {
        const auto id = selectedId();
        applyChange([this, id, width](QString& error) { return model_->setWidth(id, width, error); }, id);
    });
    connect(label_, &QLineEdit::editingFinished, this, [this] {
        applyTextSetting(QStringLiteral("label"), label_->text());
    });
    connect(accent_, &QComboBox::currentIndexChanged, this, [this] {
        applyTextSetting(QStringLiteral("accent"), accent_->currentData().toString());
    });
    connect(showDate_, &QCheckBox::toggled, this, &WidgetSettingsDialog::applyWorldClockSetting);
    connect(applyTimeZone_, &QPushButton::clicked, this, &WidgetSettingsDialog::applyWorldClockTimeZone);
    if (model_) {
        connect(model_, &WidgetHostModel::configurationChanged, this, [this] {
            if (!changing_) refreshList();
        });
        connect(model_, &WidgetHostModel::runningChanged, this, [this] {
            if (!changing_) refreshList();
        });
        connect(model_, &WidgetHostModel::widgetError, this,
                [this](const QString& id, const QString& error) {
            if (!changing_) refreshList();
            if (id == selectedId()) error_->setText(error);
        });
    }
    for (auto* button : findChildren<QPushButton*>()) button->setAutoDefault(false);
    refreshList();
}

void WidgetSettingsDialog::reject()
{
    emit backRequested();
}

QString WidgetSettingsDialog::selectedId() const
{
    const auto* item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

void WidgetSettingsDialog::refreshList(const QString& preferredId)
{
    if (refreshing_) return;
    const auto keepId = preferredId.isEmpty() ? selectedId() : preferredId;
    const int previousRow = list_->currentRow();
    refreshing_ = true;
    const QSignalBlocker blockList(list_);
    list_->clear();
    bool anyVisible = false;
    if (model_) {
        const auto configuration = model_->configuration();
        for (std::size_t index = 0; index < configuration.size(); ++index) {
            const auto& widget = configuration[index];
            const int row = static_cast<int>(index);
            auto name = roleValue(model_, row, "displayName").toString();
            if (name.isEmpty()) name = widget.typeId;
            if (widget.typeId == QStringLiteral("world-clock")) {
                name = worldClockTimeZone(widget.settings) + QStringLiteral(" · ") + name;
            }
            const auto label = widget.settings.value(QStringLiteral("label")).toString();
            if (!label.isEmpty()) name = label + QStringLiteral(" · ") + name;
            const auto lifecycle = roleValue(model_, row, "lifecycleState").toString();
            auto state = lifecycle == QStringLiteral("active") ? uiText("ui.panelde") : uiText("ui.hazir");
            if (!widget.enabled) state = uiText("ui.kapali");
            else if (!widget.visible) state = uiText("ui.gizli");
            else if (state.isEmpty()) state = uiText("ui.hazir");
            if (!roleValue(model_, row, "errorMessage").toString().isEmpty()) {
                state = uiText("ui.hata");
            }
            auto* item = new QListWidgetItem(name + QLatin1Char('\n') + state, list_);
            item->setData(Qt::UserRole, widget.instanceId);
            item->setToolTip(name);
            if (widget.instanceId == keepId) list_->setCurrentItem(item);
            anyVisible = anyVisible || (widget.enabled && widget.visible);
        }
    }
    if (!list_->currentItem() && list_->count() > 0) {
        list_->setCurrentRow(std::clamp(previousRow, 0, list_->count() - 1));
    }
    emptyHint_->setText(list_->count() == 0 ?
        uiText("ui.henuz_widget_yok_widget_ekle_dugmesiyle_baslayabilirsin") :
        uiText("ui.tum_moduller_kapali_veya_gizli_panelde_bos_durum_bilgisi"));
    emptyHint_->setVisible(!anyVisible);
    add_->setEnabled(model_ != nullptr);
    refreshing_ = false;
    refreshEditor();
}

void WidgetSettingsDialog::refreshEditor()
{
    refreshing_ = true;
    const QSignalBlocker blockEnabled(enabled_);
    const QSignalBlocker blockVisible(visible_);
    const QSignalBlocker blockWidth(width_);
    const QSignalBlocker blockLabel(label_);
    const QSignalBlocker blockAccent(accent_);
    const QSignalBlocker blockShowDate(showDate_);
    const QSignalBlocker blockTimeZone(timeZone_);
    const auto configuration = model_ ? model_->configuration() : WidgetConfigurations{};
    const auto id = selectedId();
    const auto found = std::find_if(configuration.begin(), configuration.end(),
        [&id](const auto& widget) { return widget.instanceId == id; });
    const bool hasSelection = found != configuration.end();
    const bool isSample = hasSelection && found->typeId == QStringLiteral("sample");
    const bool isWorldClock = hasSelection && found->typeId == QStringLiteral("world-clock");
    const bool isCountdown = hasSelection && found->typeId == QStringLiteral("countdown");
    const bool isFocus = hasSelection && found->typeId == QStringLiteral("focus-timer");
    const auto* descriptor = hasSelection ? model_->registry().descriptor(found->typeId) : nullptr;
    const bool contentSized = descriptor && descriptor->contentSized;
    const bool editableLabel = isSample || isWorldClock || isCountdown || isFocus;
    selectionTitle_->setText(hasSelection ? roleValue(model_, list_->currentRow(), "displayName").toString()
                                         : uiText("ui.bir_widget_sec"));
    enabled_->setEnabled(hasSelection);
    visible_->setEnabled(hasSelection);
    width_->setEnabled(hasSelection && !contentSized);
    width_->setToolTip(contentSized ?
        uiText("ui.bu_widgetin_genisligi_icerigine_gore_ayarlanir") : QString{});
    label_->setEnabled(editableLabel);
    label_->setPlaceholderText(isWorldClock ? uiText("ui.bos_varsayilan_saat_adi") : QString{});
    label_->setToolTip(isWorldClock ?
        uiText("ui.panelde_gorunecek_saat_adi_bos_birakirsan_saat_diliminin") : QString{});
    accent_->setEnabled(isSample);
    showDate_->setEnabled(isWorldClock);
    detailsForm_->setRowVisible(width_, hasSelection && !contentSized);
    detailsForm_->setRowVisible(label_, editableLabel);
    detailsForm_->setRowVisible(accent_, isSample);
    detailsForm_->setRowVisible(showDate_, isWorldClock);
    worldClockZoneEditor_->setVisible(isWorldClock);
    detailsHint_->setText(isFocus ?
        uiText("ui.sureyi_degistirmek_veya_oturumu_baslatmak_icin_paneldeki") : isCountdown ?
        uiText("ui.hedef_saat_ve_tekrar_ayarlari_icin_paneldeki_geri_sayima") :
        (isWorldClock ? uiText("ui.saat_dilimini_uygula_ile_kaydet_diger_degisiklikler_otom") :
         uiText("ui.degisiklikler_otomatik_kaydedilir")));
    detailsHint_->setVisible(hasSelection);
    remove_->setEnabled(hasSelection);
    up_->setEnabled(hasSelection && list_->currentRow() > 0);
    down_->setEnabled(hasSelection && list_->currentRow() + 1 < list_->count());
    enabled_->setChecked(hasSelection && found->enabled);
    visible_->setChecked(hasSelection && found->visible);
    width_->setValue(hasSelection ? found->widthDip : MinimumWidgetWidthDip);
    label_->setText(isWorldClock ? worldClockLabel(found->settings) :
        (editableLabel ? found->settings.value(QStringLiteral("label")).toString() : QString{}));
    const auto accent = isSample ? found->settings.value(QStringLiteral("accent")).toString() : QString{};
    accent_->setCurrentIndex(accent_->findData(accent));
    showDate_->setChecked(isWorldClock && found->settings.value(QStringLiteral("show_date")).toBool());
    timeZone_->setEditText(isWorldClock ? worldClockTimeZone(found->settings) : QString{});
    applyTimeZone_->setEnabled(isWorldClock);
    error_->setText(hasSelection ? roleValue(model_, list_->currentRow(), "errorMessage").toString() : QString{});
    refreshing_ = false;
}

void WidgetSettingsDialog::applyChange(const std::function<bool(QString&)>& change,
                                      const QString& preferredId)
{
    if (refreshing_ || changing_ || !model_) return;
    const auto keepId = preferredId.isEmpty() ? selectedId() : preferredId;
    changing_ = true;
    QString error;
    const bool ok = change(error);
    changing_ = false;
    refreshList(keepId);
    if (!ok) error_->setText(error);
}

void WidgetSettingsDialog::applyTextSetting(const QString& key, const QString& value)
{
    if (refreshing_ || changing_ || !model_) return;
    const auto id = selectedId();
    const auto configuration = model_->configuration();
    const auto found = std::find_if(configuration.begin(), configuration.end(),
        [&id](const auto& widget) { return widget.instanceId == id; });
    if (found == configuration.end()) return;
    const bool isClockLabel = found->typeId == QStringLiteral("world-clock") &&
        key == QStringLiteral("label");
    const bool isCountdownLabel = found->typeId == QStringLiteral("countdown") && key == QStringLiteral("label");
    const bool isFocusLabel = found->typeId == QStringLiteral("focus-timer") && key == QStringLiteral("label");
    if (found->typeId != QStringLiteral("sample") && !isClockLabel && !isCountdownLabel && !isFocusLabel) return;
    const auto normalizedValue = isClockLabel ? value.trimmed() : value;
    auto settings = found->settings;
    if (isClockLabel && settings.value(key).toString().trimmed().isEmpty()) {
        const auto defaultLabel = WorldClockWidget::defaultLabelForTimeZone(worldClockTimeZone(settings));
        if (normalizedValue.isEmpty() || normalizedValue == defaultLabel) {
            label_->setText(defaultLabel);
            return;
        }
    }
    if (settings.value(key).toString() == normalizedValue) {
        if (isClockLabel) label_->setText(normalizedValue);
        return;
    }
    settings.insert(key, normalizedValue);
    const auto pendingTimeZone = isClockLabel ? timeZone_->currentText() : QString{};
    applyChange([this, id, settings](QString& error) {
        return model_->setWidgetSettings(id, settings, error);
    }, id);
    if (isClockLabel && selectedId() == id) timeZone_->setEditText(pendingTimeZone);
}

void WidgetSettingsDialog::applyWorldClockSetting(bool showDate)
{
    if (refreshing_ || changing_ || !model_) return;
    const auto id = selectedId();
    const auto configuration = model_->configuration();
    const auto found = std::find_if(configuration.begin(), configuration.end(),
        [&id](const auto& widget) { return widget.instanceId == id; });
    if (found == configuration.end() || found->typeId != QStringLiteral("world-clock")) return;
    auto settings = found->settings;
    if (settings.value(QStringLiteral("show_date")).toBool() == showDate) return;
    settings.insert(QStringLiteral("show_date"), showDate);
    applyChange([this, id, settings](QString& error) {
        return model_->setWidgetSettings(id, settings, error);
    }, id);
}

void WidgetSettingsDialog::applyWorldClockTimeZone()
{
    if (refreshing_ || changing_ || !model_) return;
    const auto id = selectedId();
    const auto configuration = model_->configuration();
    const auto found = std::find_if(configuration.begin(), configuration.end(),
        [&id](const auto& widget) { return widget.instanceId == id; });
    if (found == configuration.end() || found->typeId != QStringLiteral("world-clock")) return;
    const auto zone = timeZone_->currentText().trimmed();
    if (zone.isEmpty() || !QTimeZone(zone.toUtf8()).isValid()) {
        error_->setText(uiText("ui.gecerli_bir_iana_saat_dilimi_girin_ornek_europe_istanbul"));
        return;
    }
    auto settings = found->settings;
    if (worldClockTimeZone(settings) == zone) {
        timeZone_->setEditText(zone);
        error_->clear();
        return;
    }
    settings.insert(QStringLiteral("time_zone"), zone);
    applyChange([this, id, settings](QString& error) {
        return model_->setWidgetSettings(id, settings, error);
    }, id);
}

} // namespace w11widget
