#pragma once

#include <QString>
#include <QWidget>

class SettingsSecuritySection
{
public:
    static QWidget* build(const QString& id, QWidget* owner);
};
