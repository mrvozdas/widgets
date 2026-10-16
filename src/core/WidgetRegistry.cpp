#include "core/WidgetRegistry.h"

#include "core/WidgetConfiguration.h"
#include "widgets/SampleWidget.h"
#include "widgets/CountdownWidget.h"
#include "widgets/FocusTimerWidget.h"
#include "widgets/WorldClockWidget.h"

#include <QDebug>
#include <QRegularExpression>
#include <exception>
#include <utility>

namespace w11widget {

bool WidgetRegistry::registerWidget(const WidgetDescriptor& descriptor, Factory factory, QString& error)
{
    error.clear();
    static const QRegularExpression typePattern(QStringLiteral("^[a-z][a-z0-9_.-]{0,63}$"));
    if (!typePattern.match(descriptor.typeId).hasMatch()) {
        error = QStringLiteral("Widget type ID must use 1–64 lowercase ASCII letters, digits, '.', '_' or '-'.");
        return false;
    }
    if (descriptor.displayName.trimmed().isEmpty() || descriptor.displayName.size() > 80) {
        error = QStringLiteral("Widget display name must contain 1–80 characters.");
        return false;
    }
    if (descriptor.apiVersion != WidgetApiVersion) {
        error = QStringLiteral("Widget API version %1 is unsupported; the host requires version %2.")
                    .arg(descriptor.apiVersion).arg(WidgetApiVersion);
        return false;
    }
    const auto& source = descriptor.qmlSource;
    if (!source.isValid() || source.scheme() != QStringLiteral("qrc") ||
        !source.host().isEmpty() || !source.path().startsWith(QLatin1Char('/')) ||
        !source.path().endsWith(QStringLiteral(".qml")) || source.path().contains(QStringLiteral("..")) ||
        source.hasQuery() || source.hasFragment()) {
        error = QStringLiteral("Built-in widget QML must use an absolute qrc:/ resource URL ending in .qml.");
        return false;
    }
    if (!factory) {
        error = QStringLiteral("Widget factory is empty.");
        return false;
    }
    if (descriptor.defaultWidthDip < MinimumWidgetWidthDip ||
        descriptor.defaultWidthDip > MaximumWidgetWidthDip) {
        error = QStringLiteral("Widget default width must be %1–%2 DIP.")
                    .arg(MinimumWidgetWidthDip).arg(MaximumWidgetWidthDip);
        return false;
    }
    if (this->descriptor(descriptor.typeId)) {
        error = QStringLiteral("Widget type '%1' is already registered.").arg(descriptor.typeId);
        return false;
    }
    descriptors_.push_back(descriptor);
    factories_.push_back(std::move(factory));
    return true;
}

const WidgetDescriptor* WidgetRegistry::descriptor(const QString& typeId) const
{
    for (const auto& item : descriptors_) {
        if (item.typeId == typeId) return &item;
    }
    return nullptr;
}

std::unique_ptr<IWidget> WidgetRegistry::create(const QString& typeId, QString& error) const
{
    error.clear();
    for (std::size_t index = 0; index < descriptors_.size(); ++index) {
        if (descriptors_[index].typeId != typeId) continue;
        try {
            auto result = factories_[index]();
            if (!result) error = QStringLiteral("Widget factory '%1' returned no module.").arg(typeId);
            return result;
        } catch (const std::exception& exception) {
            error = QStringLiteral("Widget factory '%1' threw: %2").arg(typeId, QString::fromUtf8(exception.what()));
        } catch (...) {
            error = QStringLiteral("Widget factory '%1' threw an unknown exception.").arg(typeId);
        }
        return nullptr;
    }
    error = QStringLiteral("Widget type '%1' is not registered.").arg(typeId);
    return nullptr;
}

WidgetRegistry builtInWidgetRegistry()
{
    WidgetRegistry registry;
    const WidgetDescriptor sample{
        QStringLiteral("sample"), QStringLiteral("Örnek sayaç"), WidgetApiVersion,
        QUrl(QStringLiteral("qrc:/qml/SampleWidget.qml")),
        {{QStringLiteral("label"), QStringLiteral("Örnek")},
         {QStringLiteral("accent"), QStringLiteral("#8ddbc2")}}, 120
    };
    const WidgetDescriptor worldClock{
        QStringLiteral("world-clock"), QStringLiteral("Dünya saati"), WidgetApiVersion,
        QUrl(QStringLiteral("qrc:/qml/WorldClockWidget.qml")),
        {{QStringLiteral("show_date"), false},
         {QStringLiteral("time_zone"), QStringLiteral("UTC")}}, WorldClockWidget::PreferredWidthDip, true
    };
    const WidgetDescriptor countdown{
        QStringLiteral("countdown"), QStringLiteral("Geri sayım"), WidgetApiVersion,
        QUrl(QStringLiteral("qrc:/qml/CountdownWidget.qml")),
        CountdownWidget::defaultSettings(), CountdownWidget::PreferredWidthDip, true
    };
    const WidgetDescriptor focusTimer{
        QStringLiteral("focus-timer"), QStringLiteral("Odak sayacı"), WidgetApiVersion,
        QUrl(QStringLiteral("qrc:/qml/FocusTimerWidget.qml")), FocusTimerWidget::defaultSettings(), 88, true
    };
    QString error;
    if (!registry.registerWidget(sample, [] { return std::make_unique<SampleWidget>(); }, error))
        qWarning().noquote() << error;
    if (!registry.registerWidget(worldClock, [] { return std::make_unique<WorldClockWidget>(); }, error))
        qWarning().noquote() << error;
    if (!registry.registerWidget(countdown, [] { return std::make_unique<CountdownWidget>(); }, error))
        qWarning().noquote() << error;
    if (!registry.registerWidget(focusTimer, [] { return std::make_unique<FocusTimerWidget>(); }, error))
        qWarning().noquote() << error;
    return registry;
}

} // namespace w11widget
