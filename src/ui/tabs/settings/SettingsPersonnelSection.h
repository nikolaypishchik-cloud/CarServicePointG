#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class SettingsPersonnelSection
{
public:
    static QWidget* build(const QString& id,
                          QWidget* owner,
                          const std::function<QWidget*()>& manageBuilder,
                          const std::function<void()>& refreshAll = {});
};
