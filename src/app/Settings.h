#pragma once
#include <QSettings>
#include <QString>
#include <QVariant>

// Thin wrapper over QSettings to keep keys centralized.
class Settings {
public:
    static QVariant get(const QString& key, const QVariant& def = {});
    static void set(const QString& key, const QVariant& value);

    // Common keys
    static QString keyTheme();   // "ui/theme"
    static QString keySounds();  // "ui/sounds_enabled"
};
