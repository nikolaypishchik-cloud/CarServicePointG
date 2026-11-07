#include "core/services/settings/UiStateSettingsService.h"

#include "infrastructure/db/sqlite/Db.h"

namespace UiStateSettingsService {

QString loadString(const QString& key, const QString& defaultValue)
{
    return Database::getSetting(key, defaultValue).toString();
}

bool saveString(const QString& key, const QString& value, bool* changed)
{
    const QString current = Database::getSetting(key, QString()).toString();
    if (changed) *changed = (current != value);
    return Database::setSetting(key, value);
}

QVariant loadValue(const QString& key, const QVariant& defaultValue)
{
    return Database::getSetting(key, defaultValue);
}

bool saveValue(const QString& key, const QVariant& value, bool* changed)
{
    const QVariant current = Database::getSetting(key, QVariant());
    if (changed) *changed = (current != value);
    return Database::setSetting(key, value);
}

} // namespace UiStateSettingsService
