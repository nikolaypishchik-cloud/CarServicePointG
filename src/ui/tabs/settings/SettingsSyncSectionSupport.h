#pragma once

#include <QWidget>
#include <functional>

class SettingsSyncSectionSupport
{
public:
    static QWidget* build(QWidget* owner, const std::function<void()>& onRefreshAll);
};
