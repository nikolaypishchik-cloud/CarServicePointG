#pragma once

#include <QWidget>
#include <functional>

class SettingsGoogleSheetsSectionSupport
{
public:
    static QWidget* build(QWidget* owner,
                          const std::function<void(const QString&, QWidget*)>& importStarter);
};
