#include "runtime/RuntimeSettings.h"

#include "app/Settings.h"

QVariant RuntimeSettings::get(const QString& key, const QVariant& def)
{
    return Settings::get(key, def);
}

void RuntimeSettings::set(const QString& key, const QVariant& value)
{
    Settings::set(key, value);
}
