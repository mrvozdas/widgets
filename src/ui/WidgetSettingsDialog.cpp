#include "ui/WidgetSettingsDialog.h"

#include "core/WidgetHostModel.h"
#include "widgets/WorldClockWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

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
    setWindowTitle(QStringLiteral("w11widget — Modüller"));
    setModal(false);
    setMinimumSize(500, 600);

    auto* layout = new QVBoxLayout(this);
    auto* back = new QPushButton(QStringLiteral("Geri"), this);
    back->setObjectName(QStringLiteral("widgetSettingsBack"));
    layout->addWidget(back, 0, Qt::AlignLeft);
    auto* explanation = new QLabel(QStringLiteral(
        "Widget'ları ekle, sırala ve düzenle. Her saat ayrı bir widget'tır. "
        "Saat dilimi Uygula düğmesiyle kaydedilir; diğer değişiklikler otomatik kaydedilir."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("widgetList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list_, 1);

    auto* actions = new QGridLayout;
    add_ = new QPushButton(QStringLiteral("Örnek ekle"), this);
    add_->setObjectName(QStringLiteral("addWidget"));
    worldClock_ = new QPushButton(QStringLiteral("Saat ekle"), this);
    worldClock_->setObjectName(QStringLiteral("addWorldClock"));
    countdown_ = new QPushButton(QStringLiteral("Geri sayım ekle"), this);
    countdown_->setObjectName(QStringLiteral("addCountdown"));
    remove_ = new QPushButton(QStringLiteral("Kaldır"), this);
    remove_->setObjectName(QStringLiteral("removeWidget"));
    up_ = new QPushButton(QStringLiteral("Yukarı"), this);
    up_->setObjectName(QStringLiteral("moveWidgetUp"));
    down_ = new QPushButton(QStringLiteral("Aşağı"), this);
    down_->setObjectName(QStringLiteral("moveWidgetDown"));
    const std::array<QPushButton*, 6> actionButtons{add_, worldClock_, countdown_, remove_, up_, down_};
    for (int index = 0; index < static_cast<int>(actionButtons.size()); ++index) {
        auto* button = actionButtons[static_cast<std::size_t>(index)];
        button->setMinimumHeight(30);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        actions->addWidget(button, index / 3, index % 3);
    }
    layout->addLayout(actions);

    emptyHint_ = new QLabel(this);
    emptyHint_->setObjectName(QStringLiteral("widgetEmptyHint"));
    emptyHint_->setWordWrap(true);
    layout->addWidget(emptyHint_);

    auto* form = new QFormLayout;
    enabled_ = new QCheckBox(QStringLiteral("Modülü çalıştır"), this);
    enabled_->setObjectName(QStringLiteral("widgetEnabled"));
    visible_ = new QCheckBox(QStringLiteral("Panelde göster"), this);
    visible_->setObjectName(QStringLiteral("widgetVisible"));
    width_ = new QSpinBox(this);
    width_->setObjectName(QStringLiteral("widgetWidth"));
    width_->setRange(MinimumWidgetWidthDip, MaximumWidgetWidthDip);
    width_->setSuffix(QStringLiteral(" px"));
    label_ = new QLineEdit(this);
    label_->setObjectName(QStringLiteral("widgetLabel"));
    label_->setMaxLength(40);
    label_->setClearButtonEnabled(true);
    accent_ = new QComboBox(this);
    accent_->setObjectName(QStringLiteral("widgetAccent"));
    accent_->addItem(QStringLiteral("Nane"), QStringLiteral("#8ddbc2"));
    accent_->addItem(QStringLiteral("Mavi"), QStringLiteral("#91bfff"));
    accent_->addItem(QStringLiteral("Sarı"), QStringLiteral("#ffd48a"));
    accent_->addItem(QStringLiteral("Pembe"), QStringLiteral("#f3a8cf"));
    showDate_ = new QCheckBox(QStringLiteral("Tarihi göster"), this);
    showDate_->setObjectName(QStringLiteral("worldClockShowDate"));
    form->addRow(enabled_);
    form->addRow(visible_);
    form->addRow(QStringLiteral("Genişlik:"), width_);
    form->addRow(QStringLiteral("Etiket:"), label_);
    form->addRow(QStringLiteral("Vurgu rengi:"), accent_);
    form->addRow(showDate_);
    layout->addLayout(form);

    worldClockZoneEditor_ = new QWidget(this);
    worldClockZoneEditor_->setObjectName(QStringLiteral("worldClockZoneEditor"));
    auto* zoneLayout = new QVBoxLayout(worldClockZoneEditor_);
    zoneLayout->setContentsMargins(0, 0, 0, 0);
    zoneLayout->addWidget(new QLabel(QStringLiteral("Bu saatin dilimi:"), worldClockZoneEditor_));
    auto* zoneActions = new QHBoxLayout;
    timeZone_ = new QComboBox(worldClockZoneEditor_);
    timeZone_->setObjectName(QStringLiteral("worldClockTimeZone"));
    timeZone_->setEditable(true);
    timeZone_->setInsertPolicy(QComboBox::NoInsert);
    QList<QByteArray> availableZones = QTimeZone::availableTimeZoneIds();
    std::sort(availableZones.begin(), availableZones.end());
    for (const auto& zone : availableZones) timeZone_->addItem(QString::fromLatin1(zone), zone);
    applyTimeZone_ = new QPushButton(QStringLiteral("Uygula"), worldClockZoneEditor_);
    applyTimeZone_->setObjectName(QStringLiteral("applyWorldClockTimeZone"));
    applyTimeZone_->setAutoDefault(false);
    zoneActions->addWidget(timeZone_, 1);
    zoneActions->addWidget(applyTimeZone_);
    zoneLayout->addLayout(zoneActions);
    layout->addWidget(worldClockZoneEditor_);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("widgetError"));
    error_->setWordWrap(true);
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_->setStyleSheet(QStringLiteral("color: #dc706b;"));
    layout->addWidget(error_);
    connect(back, &QPushButton::clicked, this, &WidgetSettingsDialog::backRequested);

    connect(list_, &QListWidget::currentRowChanged, this, [this] {
        if (!refreshing_) refreshEditor();
    });
    connect(add_, &QPushButton::clicked, this, [this] {
        const auto previous = model_->configuration();
        applyChange([this](QString& error) {
            return model_->addWidget(QStringLiteral("sample"), error);
        });
        const auto current = model_->configuration();
        if (current.size() > previous.size()) refreshList(current.back().instanceId);
    });
    connect(worldClock_, &QPushButton::clicked, this, [this] {
        const auto previous = model_->configuration();
        applyChange([this](QString& error) {
            return model_->addWidget(QStringLiteral("world-clock"), error);
        });
        const auto current = model_->configuration();
        if (current.size() > previous.size()) refreshList(current.back().instanceId);
    });
    connect(countdown_, &QPushButton::clicked, this, [this] {
        const auto previous = model_->configuration();
        applyChange([this](QString& error) {
            return model_->addWidget(QStringLiteral("countdown"), error);
        });
        const auto current = model_->configuration();
        if (current.size() > previous.size()) refreshList(current.back().instanceId);
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
            auto state = roleValue(model_, row, "lifecycleState").toString();
            if (!widget.enabled) state = QStringLiteral("Kapalı");
            else if (!widget.visible) state = QStringLiteral("Gizli");
            else if (state.isEmpty()) state = QStringLiteral("Hazır");
            if (!roleValue(model_, row, "errorMessage").toString().isEmpty()) {
                state = QStringLiteral("Hata");
            }
            auto* item = new QListWidgetItem(name + QStringLiteral(" — ") + state, list_);
            item->setData(Qt::UserRole, widget.instanceId);
            item->setToolTip(widget.instanceId + QStringLiteral(" / ") + widget.typeId);
            if (widget.instanceId == keepId) list_->setCurrentItem(item);
            anyVisible = anyVisible || (widget.enabled && widget.visible);
        }
    }
    if (!list_->currentItem() && list_->count() > 0) {
        list_->setCurrentRow(std::clamp(previousRow, 0, list_->count() - 1));
    }
    emptyHint_->setText(list_->count() == 0 ?
        QStringLiteral("Henüz widget yok. Saat ekle düğmesiyle başlayabilirsin.") :
        QStringLiteral("Tüm modüller kapalı veya gizli. Panelde boş durum bilgisi gösterilir."));
    emptyHint_->setVisible(!anyVisible);
    add_->setEnabled(model_ != nullptr);
    worldClock_->setEnabled(model_ != nullptr);
    countdown_->setEnabled(model_ != nullptr);
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
    enabled_->setEnabled(hasSelection);
    visible_->setEnabled(hasSelection);
    width_->setEnabled(hasSelection && !isWorldClock && !isCountdown);
    width_->setToolTip(isWorldClock || isCountdown ?
        QStringLiteral("Saat ve geri sayım genişliği içeriğine göre ayarlanır.") : QString{});
    label_->setEnabled(isSample || isWorldClock);
    label_->setPlaceholderText(isWorldClock ? QStringLiteral("Boş: varsayılan saat adı") : QString{});
    label_->setToolTip(isWorldClock ?
        QStringLiteral("Panelde görünecek saat adı. Boş bırakırsan saat diliminin varsayılan adı kullanılır.") : QString{});
    accent_->setEnabled(isSample);
    showDate_->setEnabled(isWorldClock);
    worldClockZoneEditor_->setVisible(isWorldClock);
    remove_->setEnabled(hasSelection);
    up_->setEnabled(hasSelection && list_->currentRow() > 0);
    down_->setEnabled(hasSelection && list_->currentRow() + 1 < list_->count());
    enabled_->setChecked(hasSelection && found->enabled);
    visible_->setChecked(hasSelection && found->visible);
    width_->setValue(hasSelection ? found->widthDip : MinimumWidgetWidthDip);
    label_->setText(isWorldClock ? worldClockLabel(found->settings) :
        (isSample ? found->settings.value(QStringLiteral("label")).toString() : QString{}));
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
    if (found->typeId != QStringLiteral("sample") && !isClockLabel) return;
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
        error_->setText(QStringLiteral("Geçerli bir IANA saat dilimi girin (örnek: Europe/Istanbul)."));
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
