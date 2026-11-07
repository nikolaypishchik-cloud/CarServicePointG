#pragma once

#include <QString>
#include <QVariant>

namespace UiStateSettingsService {

QString loadString(const QString& key, const QString& defaultValue = QString());
bool saveString(const QString& key, const QString& value, bool* changed = nullptr);
QVariant loadValue(const QString& key, const QVariant& defaultValue = QVariant());
bool saveValue(const QString& key, const QVariant& value, bool* changed = nullptr);

} // namespace UiStateSettingsService
