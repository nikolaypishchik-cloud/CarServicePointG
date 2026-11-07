#include "core/services/settings/ThemePreferenceService.h"

#include "infrastructure/db/sqlite/Db.h"

namespace ThemePreferenceService {

QString loadTheme()
{
    QString theme = Database::getSetting(QStringLiteral("ui.theme"), QStringLiteral("graphite")).toString().trimmed().toLower();
    if (theme != QStringLiteral("light") && theme != QStringLiteral("graphite")) theme = QStringLiteral("graphite");
    return theme;
}

bool saveTheme(const QString& themeName, bool* changed)
{
    const QString normalized = themeName.trimmed().toLower() == QStringLiteral("light") ? QStringLiteral("light") : QStringLiteral("graphite");
    const QString current = loadTheme();
    if (changed) *changed = (current != normalized);
    Database::setSetting(QStringLiteral("ui.theme"), normalized);
    return true;
}

} // namespace ThemePreferenceService
