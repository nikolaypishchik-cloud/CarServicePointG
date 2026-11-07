#pragma once

#include <QString>

namespace StoreNameNormalizer {

QString canonKey(const QString& raw);
QString canonicalDisplayName(const QString& raw);
QString canonicalDisplayNameOrDefault(const QString& raw,
                                     const QString& defaultValue = QString::fromUtf8("(без магазину)"));

} // namespace StoreNameNormalizer
