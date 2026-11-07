#include "core/services/settings/UiSettingsService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/util/SoundSupport.h"

namespace UiSettingsService {

bool saveFontSettings(const FontSettingsPayload& payload, bool* changed)
{
    const QString family = payload.fontFamily.trimmed().isEmpty() ? QStringLiteral("Times New Roman") : payload.fontFamily.trimmed();
    const int fontSize = payload.fontSize;
    const int tableFontSize = payload.tableFontSize;
    const int headingFontSize = payload.headingFontSize;
    const QString fontColor = payload.fontColor.trimmed().isEmpty() ? QStringLiteral("#1f2434") : payload.fontColor.trimmed();

    const bool hasChanges =
        Database::getSetting(QStringLiteral("settings.ui.font_family"), QStringLiteral("Times New Roman")).toString().trimmed() != family ||
        Database::getSetting(QStringLiteral("settings.ui.font_size"), 13).toInt() != fontSize ||
        Database::getSetting(QStringLiteral("settings.ui.table_font_size"), 13).toInt() != tableFontSize ||
        Database::getSetting(QStringLiteral("settings.ui.heading_font_size"), 13).toInt() != headingFontSize ||
        Database::getSetting(QStringLiteral("settings.ui.font_color"), QStringLiteral("#1f2434")).toString().trimmed().compare(fontColor, Qt::CaseInsensitive) != 0;
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.ui.font_family"), family);
    Database::setSetting(QStringLiteral("settings.ui.font_size"), fontSize);
    Database::setSetting(QStringLiteral("settings.ui.table_font_size"), tableFontSize);
    Database::setSetting(QStringLiteral("settings.ui.heading_font_size"), headingFontSize);
    Database::setSetting(QStringLiteral("settings.ui.font_color"), fontColor);
    return true;
}

bool saveEffectsSettings(const EffectsSettingsPayload& payload, bool* changed)
{
    const QString level = payload.effectsLevel.trimmed().isEmpty() ? QStringLiteral("ultra") : payload.effectsLevel.trimmed();
    const bool hasChanges =
        Database::getSetting(QStringLiteral("settings.ui.effects_level"), QStringLiteral("ultra")).toString().trimmed() != level ||
        Database::getSetting(QStringLiteral("settings.ui.overlay_percent"), 21).toInt() != payload.overlayPercent;
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.ui.effects_level"), level);
    Database::setSetting(QStringLiteral("settings.ui.overlay_percent"), payload.overlayPercent);
    return true;
}

bool saveSoundEnabled(bool enabled, bool* changed)
{
    const bool currentModern = Database::getSetting(QStringLiteral("settings.sounds.enabled"), true).toBool();
    const bool currentLegacy = Database::getSetting(QStringLiteral("ui/sounds_enabled"), true).toBool();
    const bool hasChanges = currentModern != enabled || currentLegacy != enabled;
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.sounds.enabled"), enabled);
    Database::setSetting(QStringLiteral("ui/sounds_enabled"), enabled);
    SoundSupport::invalidateSettingsCache();
    return true;
}

bool saveSoundVolume(int volume, bool* changed)
{
    const bool hasChanges = Database::getSetting(QStringLiteral("settings.sounds.volume"), 60).toInt() != volume;
    if (changed) *changed = hasChanges;
    Database::setSetting(QStringLiteral("settings.sounds.volume"), volume);
    SoundSupport::invalidateSettingsCache();
    return true;
}

FontSettingsPayload loadFontSettings()
{
    FontSettingsPayload payload;
    payload.fontFamily = Database::getSetting(QStringLiteral("settings.ui.font_family"), QStringLiteral("Times New Roman")).toString().trimmed();
    payload.fontSize = Database::getSetting(QStringLiteral("settings.ui.font_size"), 13).toInt();
    payload.tableFontSize = Database::getSetting(QStringLiteral("settings.ui.table_font_size"), 13).toInt();
    payload.headingFontSize = Database::getSetting(QStringLiteral("settings.ui.heading_font_size"), 13).toInt();
    payload.fontColor = Database::getSetting(QStringLiteral("settings.ui.font_color"), QStringLiteral("#1f2434")).toString().trimmed();
    return payload;
}

EffectsSettingsPayload loadEffectsSettings()
{
    EffectsSettingsPayload payload;
    payload.effectsLevel = Database::getSetting(QStringLiteral("settings.ui.effects_level"), QStringLiteral("ultra")).toString().trimmed();
    payload.overlayPercent = Database::getSetting(QStringLiteral("settings.ui.overlay_percent"), 21).toInt();
    return payload;
}

bool loadSoundEnabled()
{
    const QVariant modern = Database::getSetting(QStringLiteral("settings.sounds.enabled"), QVariant());
    if (modern.isValid() && !modern.isNull()) return modern.toBool();
    const QVariant legacy = Database::getSetting(QStringLiteral("ui/sounds_enabled"), QVariant());
    if (legacy.isValid() && !legacy.isNull()) return legacy.toBool();
    return true;
}

int loadSoundVolume()
{
    return Database::getSetting(QStringLiteral("settings.sounds.volume"), 60).toInt();
}

} // namespace UiSettingsService
