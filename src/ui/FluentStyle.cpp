#include "ui/FluentStyle.h"

#include <QFont>
#include <QWidget>

namespace w11widget {

void applyFluentStyle(QWidget* widget)
{
    QFont font = widget->font();
    font.setFamilies({QStringLiteral("Segoe UI Variable"), QStringLiteral("Segoe UI")});
    font.setPointSize(10);
    widget->setFont(font);
    // Palette roles follow the platform theme without forcing a light or dark surface.
    widget->setStyleSheet(QStringLiteral(R"(
        QWidget { color: palette(window-text); }
        QDialog, QStackedWidget { background: palette(window); }
        QLabel { background: transparent; }
        QLabel[heading="true"] { font-size: 24px; font-weight: 600; }
        QLabel[section="true"] { font-size: 15px; font-weight: 600; }
        QLabel[muted="true"] { color: palette(mid); }
        QFrame[card="true"] { background: palette(base); border: 1px solid palette(midlight); border-radius: 8px; }
        QPushButton { min-height: 28px; padding: 3px 14px; border: 1px solid palette(midlight); border-radius: 5px; background: palette(button); }
        QPushButton:hover { border-color: palette(highlight); }
        QPushButton:pressed { background: palette(alternate-base); }
        QPushButton:focus { border: 2px solid palette(highlight); padding: 2px 13px; }
        QPushButton:disabled { color: palette(mid); }
        QPushButton[primary="true"], QPushButton[nav="true"]:checked { background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); }
        QPushButton[nav="true"] { text-align: left; min-height: 32px; border-color: transparent; background: transparent; }
        QLineEdit, QSpinBox, QComboBox { min-height: 28px; padding: 2px 7px; background: palette(base); border: 1px solid palette(midlight); border-radius: 4px; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: palette(highlight); }
        QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: palette(mid); background: palette(window); }
        QCheckBox { min-height: 28px; spacing: 8px; background: transparent; }
        QListWidget { border: 1px solid palette(midlight); border-radius: 8px; background: palette(base); padding: 4px; }
        QListWidget::item { min-height: 48px; padding: 6px 9px; border-radius: 5px; }
        QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QMenu { background: palette(base); border: 1px solid palette(midlight); padding: 5px; }
        QMenu::item { padding: 8px 22px; }
        QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QScrollArea { border: none; background: transparent; }
        QWidget#widgetFields { background: palette(base); }
    )"));
}

} // namespace w11widget
