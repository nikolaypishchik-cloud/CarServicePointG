#pragma once

#include <QWidget>
#include <functional>

class SettingsBrandingSectionSupport
{
public:
    static QWidget* build(QWidget* owner,
                          const std::function<void()>& queueRefreshAll);
};
