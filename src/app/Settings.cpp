#include "app/Settings.h"

QVariant Settings::get(const QString& key, const QVariant& def)
{
    QSettings s;
    return s.value(key, def);
}

void Settings::set(const QString& key, const QVariant& value)
{
    QSettings s;
    s.setValue(key, value);
    s.sync();
}

QString Settings::keyTheme()  { return QStringLiteral("ui/theme"); }
QString Settings::keySounds() { return QStringLiteral("ui/sounds_enabled"); }
