#include "widgets/SampleWidget.h"

#include <QRegularExpression>
#include <limits>

namespace w11widget {

SampleWidget::SampleWidget(QObject* parent) : QObject(parent) {}

bool SampleWidget::validateSettings(const QJsonObject& settings, QString& error) const
{
    error.clear();
    const auto label = settings.value(QStringLiteral("label"));
    if (!label.isString() || label.toString().trimmed().isEmpty() || label.toString().size() > 40) {
        error = QStringLiteral("Sample label must be a nonblank string of at most 40 characters.");
        return false;
    }
    static const QRegularExpression accentPattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    const auto accent = settings.value(QStringLiteral("accent"));
    if (!accent.isString() || !accentPattern.match(accent.toString()).hasMatch()) {
        error = QStringLiteral("Sample accent must be a color in #RRGGBB format.");
        return false;
    }
    for (auto item = settings.begin(); item != settings.end(); ++item) {
        if (item.key() != QStringLiteral("label") && item.key() != QStringLiteral("accent")) {
            error = QStringLiteral("Unknown sample setting '%1'.").arg(item.key());
            return false;
        }
    }
    return true;
}

bool SampleWidget::initialize(const QJsonObject& settings, QString& error)
{
    if (initialized_) {
        error = QStringLiteral("Sample module is already initialized.");
        return false;
    }
    if (!validateSettings(settings, error)) return false;
    initialized_ = true;
    return updateSettings(settings, error);
}

bool SampleWidget::updateSettings(const QJsonObject& settings, QString& error)
{
    if (!initialized_) {
        error = QStringLiteral("Sample module is not initialized.");
        return false;
    }
    if (!validateSettings(settings, error)) return false;
    const QString label = settings.value(QStringLiteral("label")).toString();
    const QString accent = settings.value(QStringLiteral("accent")).toString();
    if (label_ != label) {
        label_ = label;
        emit labelChanged();
    }
    if (accent_ != accent) {
        accent_ = accent;
        emit accentChanged();
    }
    return true;
}

bool SampleWidget::activate(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Cannot activate an uninitialized sample module.");
        return false;
    }
    if (!active_) {
        active_ = true;
        emit activeChanged();
    }
    return true;
}

bool SampleWidget::suspend(QString& error)
{
    error.clear();
    if (!initialized_) {
        error = QStringLiteral("Cannot suspend an uninitialized sample module.");
        return false;
    }
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    return true;
}

void SampleWidget::shutdown()
{
    if (active_) {
        active_ = false;
        emit activeChanged();
    }
    initialized_ = false;
}

void SampleWidget::trigger()
{
    if (!initialized_ || !active_ || counter_ == std::numeric_limits<int>::max()) return;
    ++counter_;
    emit counterChanged();
}

} // namespace w11widget
