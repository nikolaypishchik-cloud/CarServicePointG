#pragma once

#include "core/services/catalog/CatalogTypes.h"

#include <QVector>
#include <QStringList>

class CatalogQueryService {
public:
    static bool ensureSchema(QString* errorText = nullptr);
    static void ensureDerived(CatalogRowData& row);
    static QVector<CatalogRowData> loadAllRows(QString* errorText = nullptr);
    static CatalogFilterOptions buildFilterOptions(const QVector<CatalogRowData>& allRows,
                                                   const CatalogFilterState& state);
    static QVector<CatalogRowData> filterRows(const QVector<CatalogRowData>& allRows,
                                              const CatalogFilterState& state);
    static QString displayYearsRange(int yearFrom, int yearTo);
    static QString normalizedBrandFilterValue(QString value);
    static QString foldedFilterValue(QString value);
    static QString normalizedPartSearchValue(QString value);
    static QStringList activeGroupOptions();
    static QStringList modelOptionsForBrand(const QString& brand);
};
