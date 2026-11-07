#pragma once

#include <QString>

namespace ThemePreferenceService {

QString loadTheme();
bool saveTheme(const QString& themeName, bool* changed = nullptr);

} // namespace ThemePreferenceService
