#pragma once
#include <QMap>
#include <QStringList>

namespace ManualInputNormalizer {
enum class Role { Protected, Text, Technical, Vehicle, Plate, Date, Search, Planning };
struct Result { QString text; QString warning; };
Role roleForHint(const QString& hint);
QString roleKey(Role role);
Result normalize(const QString& text, Role role, const QMap<QString,QString>& custom = {});
QStringList suggestions(const QString& word, Role role);
// Optional Windows Ukrainian spell dictionary; no service, download or network.
QStringList systemSuggestions(const QString& word);
}
