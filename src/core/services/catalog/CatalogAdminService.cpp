#include "core/services/catalog/CatalogAdminService.h"

#include "core/services/LegacyAncillaryMutationLogSupport.h"
#include "core/services/catalog/CatalogMutationService.h"
#include "infrastructure/db/sqlite/Db.h"

namespace CatalogAdminService {

bool ensureImportSchema(QString* errorText)
{
    return Database::ensureCatalogImportSchema(errorText);
}

QString snapshotItem(int id)
{
    if (id <= 0) return QStringLiteral("{}");
    return LegacyAncillaryMutationLogSupport::snapshotCatalogItem(Database::db(), id);
}

SaveOutcome saveRow(const CatalogRowData& row)
{
    CatalogMutationService::SaveParams params;
    params.id = row.id;
    params.brand = row.brand;
    params.model = row.model;
    params.yearFrom = row.yearFrom;
    params.yearTo = row.yearTo;
    params.fuel = row.fuel;
    params.group = row.group;
    params.oem = row.oem;
    params.name = row.name;
    params.brandName = row.brandName;
    params.supplier = row.supplier;
    params.price = row.price;
    params.note = row.note;

    const auto result = CatalogMutationService::save(Database::db(), params);
    SaveOutcome out;
    out.ok = result.ok;
    out.errorText = result.errorText;
    out.operation = result.operation;
    out.recordId = result.recordId;
    return out;
}

SaveOutcome deleteRow(int id)
{
    SaveOutcome out;
    if (id <= 0) {
        out.errorText = QStringLiteral("Invalid catalog item id");
        return out;
    }
    const auto result = CatalogMutationService::remove(Database::db(), id);
    out.ok = result.ok;
    out.errorText = result.errorText;
    out.operation = QStringLiteral("remove");
    out.recordId = id;
    return out;
}

bool logMutation(const QString& operation,
                 int recordId,
                 const QString& beforeJson,
                 const QString& afterJson,
                 QString* errorText)
{
    return LegacyAncillaryMutationLogSupport::logCatalogMutation(Database::db(), operation, recordId, beforeJson, afterJson, errorText);
}

bool importFromOrders(CatalogImportReport* report, QString* errorText)
{
    return Database::importCatalogFromOrders(report, errorText);
}

} // namespace CatalogAdminService
