#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class SettingsUiSection
{
public:
    static QWidget* build(const QString& id,
                          QWidget* owner,
                          const std::function<void()>& onFontBack,
                          const std::function<void()>& onFontRefreshAll);
};
