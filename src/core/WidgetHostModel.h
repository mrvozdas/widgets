#pragma once

#include "core/WidgetConfiguration.h"
#include "core/WidgetRegistry.h"

#include <QAbstractListModel>
#include <QVariantMap>
#include <memory>
#include <vector>

namespace w11widget {

class WidgetHostModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning WRITE setRunning NOTIFY runningChanged)

public:
    enum Role {
        InstanceIdRole = Qt::UserRole + 1,
        TypeIdRole,
        DisplayNameRole,
        ModuleEnabledRole,
        ModuleVisibleRole,
        WidthDipRole,
        ComponentSourceRole,
        ViewModelRole,
        LifecycleStateRole,
        ErrorMessageRole
    };
    Q_ENUM(Role)

    explicit WidgetHostModel(QObject* parent = nullptr);
    explicit WidgetHostModel(WidgetRegistry registry, QObject* parent = nullptr);
    ~WidgetHostModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const WidgetConfigurations& configuration() const { return configuration_; }
    bool setConfiguration(const WidgetConfigurations& configuration, QString& error);
    bool setEnabled(const QString& instanceId, bool enabled, QString& error);
    bool setVisible(const QString& instanceId, bool visible, QString& error);
    bool moveWidget(const QString& instanceId, int targetIndex, QString& error);
    bool setWidgetSettings(const QString& instanceId, const QJsonObject& settings, QString& error);
    bool setWidth(const QString& instanceId, int widthDip, QString& error);
    bool addWidget(const QString& typeId, QString& error);
    bool removeWidget(const QString& instanceId, QString& error);
    Q_INVOKABLE QString updateWidgetSettings(const QString& instanceId, const QVariantMap& settings);
    void refreshViewModels();

    void setRunning(bool running);
    bool isRunning() const { return running_; }
    const WidgetRegistry& registry() const { return registry_; }

signals:
    void configurationChanged();
    void runningChanged();
    void widgetError(const QString& instanceId, const QString& error);

private:
    struct Entry;
    WidgetRegistry registry_;
    WidgetConfigurations configuration_;
    std::vector<std::unique_ptr<Entry>> entries_;
    bool running_ = false;

    int findInstance(const QString& instanceId, QString& error) const;
    void reconcile(Entry& entry, const WidgetConfiguration* previous);
    void synchronizeLifecycle(Entry& entry);
    void failEntry(Entry& entry, const QString& error);
    void stopEntry(Entry& entry);
    void notifyEntry(int row);
};

} // namespace w11widget
