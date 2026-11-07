#pragma once

#include <QString>

namespace CatalogImportTextUtils {

QString foldedCatalogValue(QString value);
QString normalizedCatalogLookupKey(QString value);
bool isCatalogPlaceholderCode(QString value);
QString normalizedCatalogPartCode(QString value);
QString normalizedCatalogStoreKey(QString value);
QString normalizedCatalogBrandKey(QString value);
bool isCatalogMeaningfulNameOnly(const QString& normalizedName,
                                 const QString& brandName,
                                 const QString& vehicleBrand,
                                 const QString& vehicleModel,
                                 const QString& supplier);

} // namespace CatalogImportTextUtils
