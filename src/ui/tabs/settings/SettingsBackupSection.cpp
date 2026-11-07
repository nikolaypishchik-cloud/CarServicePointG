#include "app/AppContext.h"
#include "ui/tabs/settings/AutoBackupSettingsSection.h"

#include "ui/tabs/settings/SettingsBackupSection.h"

QWidget* SettingsBackupSection::build(const QString& id, QWidget* owner)
{
    if (id != QStringLiteral("int.autobackup")) {
        return nullptr;
    }

    auto* scroll = new QScrollArea(owner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { width: 12px; background: rgba(255,255,255,0.05); margin: 2px 0; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.28); min-height: 48px; border-radius: 6px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ));

    auto* section = new AutoBackupSettingsSection(AppContext::instance().backupManager(),
                                                  AppContext::instance().crashRecoveryManager(),
                                                  AppContext::instance().restoreCoordinator(),
                                                  AppContext::instance().recoveryCoordinator(),
                                                  scroll);
    section->setStyleSheet(QStringLiteral("background: transparent;"));
    scroll->setWidget(section);
    return scroll;
}
