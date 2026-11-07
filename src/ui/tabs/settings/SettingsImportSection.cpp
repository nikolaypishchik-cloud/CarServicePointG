#include "ui/tabs/settings/SettingsImportSection.h"
#include <functional>

QWidget* SettingsImportSection::build(const QString& id,
                                      QWidget* owner,
                                      const std::function<QWidget*()>& googleSheetsBuilder)
{
    Q_UNUSED(owner);
    if (id == QStringLiteral("int.gsheets")) {
        return googleSheetsBuilder ? googleSheetsBuilder() : nullptr;
    }
    return nullptr;
}