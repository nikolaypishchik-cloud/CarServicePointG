#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class SettingsGeneralSection
{
public:
    static QWidget* build(const QString& id,
                          QWidget* owner,
                          const std::function<QWidget*()>& brandingBuilder);
};
