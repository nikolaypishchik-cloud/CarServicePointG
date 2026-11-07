#pragma once

#include <QAction>
#include <QApplication>
#include <QFontMetrics>
#include <QMenu>

namespace ContextMenuAppearance {
inline void apply(QMenu& menu)
{
    // Local parent styles must not change the appearance of popup menus.
    menu.setFont(QApplication::font());
    const QString style = QStringLiteral(
        "QMenu { color: #ffffff; padding: 8px 0px; background: #414956;"
        " border: 1px solid #87919f; border-radius: 14px; }"
        "QMenu::item { color: #ffffff; padding: 10px 136px 10px 18px;"
        " min-height: 26px; margin: 2px 8px; border: 1px solid transparent;"
        " border-radius: 10px; background: transparent; }"
        "QMenu::item:selected { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        " stop:0 #6e91be,stop:1 #4c658a); border-color: #b2bfce; }"
        "QMenu::item:disabled { color: #a3aab4; background: transparent; }"
        "QMenu::separator { height: 1px; margin: 6px 12px; background: #647080; }"
    );
    if (menu.styleSheet() != style) menu.setStyleSheet(style);
    const QFontMetrics metrics(menu.font());
    int width = 220;
    for (const auto* action : menu.actions()) {
        if (!action || action->isSeparator() || !action->isVisible()) continue;
        width = qMax(width, metrics.horizontalAdvance(action->text())
                     + metrics.horizontalAdvance(action->shortcut().toString(QKeySequence::NativeText)) + 150);
    }
    menu.setMinimumWidth(width);
}
}
