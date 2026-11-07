#pragma once

#include <QString>
#include <QWidget>
#include <QFrame>
#include <QScrollArea>

class SettingsBackupSection
{
public:
    static QWidget* build(const QString& id, QWidget* owner);
};
