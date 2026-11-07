#pragma once

#include <QString>
#include <QWidget>

class SettingsFinanceSection
{
public:
    static QWidget* build(const QString& id, QWidget* owner);
};
