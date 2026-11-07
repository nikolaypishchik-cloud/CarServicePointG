#pragma once

#include <QString>

namespace UiSettingsService {

struct FontSettingsPayload {
    QString fontFamily;
    int fontSize = 13;
    int tableFontSize = 13;
    int headingFontSize = 13;
    QString fontColor;
};

struct EffectsSettingsPayload {
    QString effectsLevel;
    int overlayPercent = 21;
};

bool saveFontSettings(const FontSettingsPayload& payload, bool* changed = nullptr);
bool saveEffectsSettings(const EffectsSettingsPayload& payload, bool* changed = nullptr);
bool saveSoundEnabled(bool enabled, bool* changed = nullptr);
bool saveSoundVolume(int volume, bool* changed = nullptr);

FontSettingsPayload loadFontSettings();
EffectsSettingsPayload loadEffectsSettings();
bool loadSoundEnabled();
int loadSoundVolume();

} // namespace UiSettingsService
