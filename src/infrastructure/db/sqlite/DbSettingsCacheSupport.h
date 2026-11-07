#pragma once

#include <QString>
#include <QVariant>

namespace DbSettingsCacheSupport {

QVariant tryGet(const QString& key, bool* found = nullptr);
void store(const QString& key, const QVariant& value);
void remove(const QString& key);
void clear();

} // namespace DbSettingsCacheSupport
