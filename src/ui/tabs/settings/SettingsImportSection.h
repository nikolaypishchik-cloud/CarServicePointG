#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class SettingsImportSection
{
public:
    static QWidget* build(const QString& id,
                          QWidget* owner,
                          const std::function<QWidget*()>& googleSheetsBuilder);
};
