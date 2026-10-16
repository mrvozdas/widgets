#include "core/WidgetHostModel.h"

#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <exception>
#include <utility>

namespace w11widget {
namespace {

Q_LOGGING_CATEGORY(widgetLog, "w11widget.widgets")

template <typename Function>
bool callModule(const QString& operation, Function&& function, QString& error)
{
    error.clear();
    try {
        if (function()) return true;
        if (error.isEmpty()) error = operation + QStringLiteral(" failed without an error message.");
    } catch (const std::exception& exception) {
        error = operation + QStringLiteral(" threw: ") + QString::fromUtf8(exception.what());
    } catch (...) {
        error = operation + QStringLiteral(" threw an unknown exception.");
    }
    return false;
}

bool validateStructure(const WidgetConfigurations& configuration, QString& error)
{
    error.clear();
    if (configuration.size() > MaximumWidgetCount) {
        error = QStringLiteral("At most %1 widgets are supported.").arg(MaximumWidgetCount);
        return false;
    }
    static const QRegularExpression instancePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$"));
    static const QRegularExpression typePattern(QStringLiteral("^[a-z][a-z0-9_.-]{0,63}$"));
    QSet<QString> identifiers;
    for (const auto& item : configuration) {
        if (!instancePattern.match(item.instanceId).hasMatch() || identifiers.contains(item.instanceId)) {
            error = QStringLiteral("Widget instance IDs must be unique, 1–64 ASCII letters/digits, '.', '_' or '-'.");
            return false;
        }
        identifiers.insert(item.instanceId);
        if (!typePattern.match(item.typeId).hasMatch()) {
            error = QStringLiteral("Widget '%1' has an invalid type ID.").arg(item.instanceId);
            return false;
        }
        if (item.widthDip < MinimumWidgetWidthDip || item.widthDip > MaximumWidgetWidthDip) {
            error = QStringLiteral("Widget '%1' width must be %2–%3 DIP.")
                        .arg(item.instanceId).arg(MinimumWidgetWidthDip).arg(MaximumWidgetWidthDip);
            return false;
        }
    }
    return true;
}

} // namespace

struct WidgetHostModel::Entry {
    WidgetConfiguration configuration;
    std::unique_ptr<IWidget> module;
    QObject* viewModel = nullptr;
    QString state = QStringLiteral("stopped");
    QString error;
};

WidgetHostModel::WidgetHostModel(QObject* parent) : WidgetHostModel(builtInWidgetRegistry(), parent)
{
    QString error;
    setConfiguration(defaultWidgetConfigurations(), error);
}

WidgetHostModel::WidgetHostModel(WidgetRegistry registry, QObject* parent)
    : QAbstractListModel(parent), registry_(std::move(registry)) {}

WidgetHostModel::~WidgetHostModel()
{
    for (auto& entry : entries_) stopEntry(*entry);
}

int WidgetHostModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant WidgetHostModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& entry = *entries_[static_cast<std::size_t>(index.row())];
    const auto& item = entry.configuration;
    const auto* descriptor = registry_.descriptor(item.typeId);
    switch (role) {
    case InstanceIdRole: return item.instanceId;
    case TypeIdRole: return item.typeId;
    case Qt::DisplayRole:
    case DisplayNameRole: return descriptor ? descriptor->displayName : item.typeId;
    case ModuleEnabledRole: return item.enabled;
    case ModuleVisibleRole: return item.visible;
    case WidthDipRole: return item.widthDip;
    case ComponentSourceRole: return descriptor ? descriptor->qmlSource : QUrl{};
    case ViewModelRole: return QVariant::fromValue(entry.viewModel);
    case LifecycleStateRole: return entry.state;
    case ErrorMessageRole: return entry.error;
    default: return {};
    }
}

QHash<int, QByteArray> WidgetHostModel::roleNames() const
{
    return {{InstanceIdRole, "instanceId"}, {TypeIdRole, "typeId"}, {DisplayNameRole, "displayName"},
            {ModuleEnabledRole, "moduleEnabled"}, {ModuleVisibleRole, "moduleVisible"},
            {WidthDipRole, "widthDip"}, {ComponentSourceRole, "componentSource"},
            {ViewModelRole, "viewModel"}, {LifecycleStateRole, "lifecycleState"},
            {ErrorMessageRole, "errorMessage"}};
}

void WidgetHostModel::stopEntry(Entry& entry)
{
    QString error;
    if (entry.module) {
        callModule(QStringLiteral("shutdown"), [&] {
            entry.module->shutdown();
            return true;
        }, error);
    }
    entry.viewModel = nullptr;
    entry.module.reset();
    entry.state = error.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("error");
    entry.error = error;
    if (!error.isEmpty()) {
        qCWarning(widgetLog).noquote() << entry.configuration.instanceId << error;
        emit widgetError(entry.configuration.instanceId, error);
    }
}

void WidgetHostModel::failEntry(Entry& entry, const QString& error)
{
    QString shutdownError;
    if (entry.module) {
        callModule(QStringLiteral("shutdown"), [&] {
            entry.module->shutdown();
            return true;
        }, shutdownError);
    }
    entry.viewModel = nullptr;
    entry.module.reset();
    entry.state = QStringLiteral("error");
    entry.error = error.isEmpty() ? QStringLiteral("Widget operation failed.") : error;
    if (!shutdownError.isEmpty()) entry.error += QStringLiteral(" ") + shutdownError;
    qCWarning(widgetLog).noquote() << entry.configuration.instanceId << entry.error;
    emit widgetError(entry.configuration.instanceId, entry.error);
}

void WidgetHostModel::reconcile(Entry& entry, const WidgetConfiguration* previous)
{
    const auto& item = entry.configuration;
    if (!item.enabled) {
        if (entry.module || entry.state != QStringLiteral("stopped")) stopEntry(entry);
        return;
    }
    const bool changedType = previous && previous->typeId != item.typeId;
    const bool changedSettings = previous && previous->settings != item.settings;
    const bool retry = !previous || changedType || changedSettings || !previous->enabled;
    if (changedType && entry.module) stopEntry(entry);

    QString error;
    if (!entry.module) {
        if (!retry && entry.state == QStringLiteral("error")) return;
        entry.module = registry_.create(item.typeId, error);
        if (!entry.module) {
            failEntry(entry, error);
            return;
        }
        if (!callModule(QStringLiteral("validateSettings"), [&] {
                return entry.module->validateSettings(item.settings, error);
            }, error) ||
            !callModule(QStringLiteral("initialize"), [&] {
                return entry.module->initialize(item.settings, error);
            }, error)) {
            failEntry(entry, error);
            return;
        }
        if (!callModule(QStringLiteral("viewModel"), [&] {
                entry.viewModel = entry.module->viewModel();
                if (!entry.viewModel) error = QStringLiteral("Widget returned no view model.");
                return entry.viewModel != nullptr;
            }, error)) {
            failEntry(entry, error);
            return;
        }
        entry.state = QStringLiteral("ready");
        entry.error.clear();
    } else if (changedSettings) {
        if (!callModule(QStringLiteral("validateSettings"), [&] {
                return entry.module->validateSettings(item.settings, error);
            }, error) ||
            !callModule(QStringLiteral("updateSettings"), [&] {
                return entry.module->updateSettings(item.settings, error);
            }, error)) {
            failEntry(entry, error);
            return;
        }
    }
    synchronizeLifecycle(entry);
}

void WidgetHostModel::synchronizeLifecycle(Entry& entry)
{
    if (!entry.module) return;
    const bool activate = running_ && entry.configuration.enabled && entry.configuration.visible;
    QString error;
    if (activate && entry.state != QStringLiteral("active")) {
        if (!callModule(QStringLiteral("activate"), [&] { return entry.module->activate(error); }, error)) {
            failEntry(entry, error);
            return;
        }
        entry.state = QStringLiteral("active");
    } else if (!activate && (entry.state == QStringLiteral("active") || !entry.configuration.visible)) {
        if (entry.state == QStringLiteral("suspended")) return;
        if (!callModule(QStringLiteral("suspend"), [&] { return entry.module->suspend(error); }, error)) {
            failEntry(entry, error);
            return;
        }
        entry.state = QStringLiteral("suspended");
    }
}

bool WidgetHostModel::setConfiguration(const WidgetConfigurations& configuration, QString& error)
{
    if (!validateStructure(configuration, error)) return false;
    if (configuration_ == configuration) return true;

    const bool sameOrder = configuration_.size() == configuration.size() &&
        std::equal(configuration_.begin(), configuration_.end(), configuration.begin(),
                   [](const auto& left, const auto& right) { return left.instanceId == right.instanceId; });
    if (!sameOrder) beginResetModel();
    auto oldEntries = std::move(entries_);
    entries_.reserve(configuration.size());
    configuration_ = configuration;
    for (const auto& item : configuration_) {
        auto found = std::find_if(oldEntries.begin(), oldEntries.end(), [&](const auto& candidate) {
            return candidate && candidate->configuration.instanceId == item.instanceId;
        });
        if (found == oldEntries.end()) {
            auto entry = std::make_unique<Entry>();
            entry->configuration = item;
            reconcile(*entry, nullptr);
            entries_.push_back(std::move(entry));
        } else {
            auto entry = std::move(*found);
            const auto previous = entry->configuration;
            entry->configuration = item;
            reconcile(*entry, &previous);
            entries_.push_back(std::move(entry));
        }
    }
    for (auto& entry : oldEntries) {
        if (entry) stopEntry(*entry);
    }
    if (!sameOrder) endResetModel();
    else if (!entries_.empty()) emit dataChanged(index(0), index(rowCount() - 1));
    emit configurationChanged();
    return true;
}

int WidgetHostModel::findInstance(const QString& instanceId, QString& error) const
{
    error.clear();
    for (std::size_t row = 0; row < configuration_.size(); ++row) {
        if (configuration_[row].instanceId == instanceId) return static_cast<int>(row);
    }
    error = QStringLiteral("Widget instance '%1' does not exist.").arg(instanceId);
    return -1;
}

void WidgetHostModel::notifyEntry(int row)
{
    emit dataChanged(index(row), index(row));
}

bool WidgetHostModel::setEnabled(const QString& instanceId, bool enabled, QString& error)
{
    const int row = findInstance(instanceId, error);
    if (row < 0) return false;
    if (configuration_[row].enabled == enabled) return true;
    const auto previous = configuration_[row];
    configuration_[row].enabled = enabled;
    entries_[row]->configuration = configuration_[row];
    reconcile(*entries_[row], &previous);
    notifyEntry(row);
    emit configurationChanged();
    return true;
}

bool WidgetHostModel::setVisible(const QString& instanceId, bool visible, QString& error)
{
    const int row = findInstance(instanceId, error);
    if (row < 0) return false;
    if (configuration_[row].visible == visible) return true;
    const auto previous = configuration_[row];
    configuration_[row].visible = visible;
    entries_[row]->configuration = configuration_[row];
    reconcile(*entries_[row], &previous);
    notifyEntry(row);
    emit configurationChanged();
    return true;
}

bool WidgetHostModel::moveWidget(const QString& instanceId, int targetIndex, QString& error)
{
    const int sourceIndex = findInstance(instanceId, error);
    if (sourceIndex < 0) return false;
    if (targetIndex < 0 || targetIndex >= rowCount()) {
        error = QStringLiteral("Widget target position is outside the list.");
        return false;
    }
    if (sourceIndex == targetIndex) return true;
    const int destination = targetIndex > sourceIndex ? targetIndex + 1 : targetIndex;
    beginMoveRows({}, sourceIndex, sourceIndex, {}, destination);
    auto entry = std::move(entries_[sourceIndex]);
    auto configuration = std::move(configuration_[sourceIndex]);
    entries_.erase(entries_.begin() + sourceIndex);
    configuration_.erase(configuration_.begin() + sourceIndex);
    entries_.insert(entries_.begin() + targetIndex, std::move(entry));
    configuration_.insert(configuration_.begin() + targetIndex, std::move(configuration));
    endMoveRows();
    emit configurationChanged();
    return true;
}

bool WidgetHostModel::setWidgetSettings(const QString& instanceId, const QJsonObject& settings, QString& error)
{
    const int row = findInstance(instanceId, error);
    if (row < 0) return false;
    if (configuration_[row].settings == settings) return true;
    const auto previous = configuration_[row];
    configuration_[row].settings = settings;
    entries_[row]->configuration = configuration_[row];
    reconcile(*entries_[row], &previous);
    notifyEntry(row);
    emit configurationChanged();
    return true;
}

QString WidgetHostModel::updateWidgetSettings(const QString& instanceId, const QVariantMap& settings)
{
    QString error;
    setWidgetSettings(instanceId, QJsonObject::fromVariantMap(settings), error);
    return error;
}

void WidgetHostModel::refreshViewModels()
{
    for (const auto& entry : entries_) {
        if (entry->viewModel != nullptr && entry->configuration.enabled) {
            QMetaObject::invokeMethod(entry->viewModel, "refreshNow", Qt::DirectConnection);
        }
    }
}

bool WidgetHostModel::setWidth(const QString& instanceId, int widthDip, QString& error)
{
    const int row = findInstance(instanceId, error);
    if (row < 0) return false;
    if (widthDip < MinimumWidgetWidthDip || widthDip > MaximumWidgetWidthDip) {
        error = QStringLiteral("Widget width must be %1–%2 DIP.").arg(MinimumWidgetWidthDip).arg(MaximumWidgetWidthDip);
        return false;
    }
    if (configuration_[row].widthDip == widthDip) return true;
    configuration_[row].widthDip = widthDip;
    entries_[row]->configuration.widthDip = widthDip;
    notifyEntry(row);
    emit configurationChanged();
    return true;
}

bool WidgetHostModel::addWidget(const QString& typeId, QString& error)
{
    error.clear();
    const auto* descriptor = registry_.descriptor(typeId);
    if (!descriptor) {
        error = QStringLiteral("Widget type '%1' is not registered.").arg(typeId);
        return false;
    }
    auto configuration = configuration_;
    configuration.push_back({QStringLiteral("widget-") + QUuid::createUuid().toString(QUuid::Id128),
                             typeId, true, true, descriptor->defaultWidthDip, descriptor->defaultSettings});
    return setConfiguration(configuration, error);
}

bool WidgetHostModel::removeWidget(const QString& instanceId, QString& error)
{
    const int row = findInstance(instanceId, error);
    if (row < 0) return false;
    auto configuration = configuration_;
    configuration.erase(configuration.begin() + row);
    return setConfiguration(configuration, error);
}

void WidgetHostModel::setRunning(bool running)
{
    if (running_ == running) return;
    running_ = running;
    for (int row = 0; row < rowCount(); ++row) {
        synchronizeLifecycle(*entries_[row]);
        notifyEntry(row);
    }
    emit runningChanged();
}

} // namespace w11widget
