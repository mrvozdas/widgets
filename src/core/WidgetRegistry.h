#pragma once

#include "core/IWidget.h"

#include <QUrl>
#include <functional>
#include <memory>
#include <vector>

namespace w11widget {

struct WidgetDescriptor {
    QString typeId;
    QString displayName;
    int apiVersion = WidgetApiVersion;
    QUrl qmlSource;
    QJsonObject defaultSettings;
    int defaultWidthDip = 120;
    bool contentSized = false;
};

class WidgetRegistry {
public:
    using Factory = std::function<std::unique_ptr<IWidget>()>;

    bool registerWidget(const WidgetDescriptor& descriptor, Factory factory, QString& error);
    const std::vector<WidgetDescriptor>& descriptors() const { return descriptors_; }
    const WidgetDescriptor* descriptor(const QString& typeId) const;
    std::unique_ptr<IWidget> create(const QString& typeId, QString& error) const;

private:
    std::vector<WidgetDescriptor> descriptors_;
    std::vector<Factory> factories_;
};

WidgetRegistry builtInWidgetRegistry();

} // namespace w11widget
