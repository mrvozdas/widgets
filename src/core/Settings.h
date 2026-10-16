#pragma once

#include "core/PanelPositioner.h"
#include "core/WidgetConfiguration.h"

#include <QString>

namespace w11widget {

struct PanelSettings {
    QString monitor = QStringLiteral("primary");
    PanelSide side = PanelSide::Left;
    int widthDip = 330;
    int offsetDip = 150;
    double opacity = 0.95;
    // Legacy schema-1 field. Load/save normalize it to false for the user's
    // always-visible policy; retaining it keeps existing settings compatible.
    bool hideForFullscreen = false;
    WidgetConfigurations widgets = defaultWidgetConfigurations();

    [[nodiscard]] bool operator==(const PanelSettings& other) const noexcept;
    [[nodiscard]] bool operator!=(const PanelSettings& other) const noexcept {
        return !(*this == other);
    }
};

struct SettingsLoadResult {
    PanelSettings settings{};
    bool ok = true;
    bool missing = false;
    QString error;
};

class SettingsStore {
public:
    explicit SettingsStore(QString filePath);
    [[nodiscard]] static QString defaultPath();
    [[nodiscard]] SettingsLoadResult load() const;
    [[nodiscard]] bool save(const PanelSettings& settings, QString& error) const;
    [[nodiscard]] QString filePath() const;

private:
    QString filePath_;
};

} // namespace w11widget
