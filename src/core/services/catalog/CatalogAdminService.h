#pragma once

#include "core/services/catalog/CatalogTypes.h"
#include "infrastructure/db/sqlite/Db.h"

class QWidget;

namespace CatalogAdminService {

bool ensureImportSchema(QString* errorText = nullptr);
QString snapshotItem(int id);

struct SaveOutcome {
    bool ok = false;
    QString errorText;
    QString operation;
    int recordId = 0;
};

SaveOutcome saveRow(const CatalogRowData& row);
SaveOutcome deleteRow(int id);
bool logMutation(const QString& operation,
                 int recordId,
                 const QString& beforeJson,
                 const QString& afterJson,
                 QString* errorText = nullptr);

bool importFromOrders(CatalogImportReport* report, QString* errorText = nullptr);

} // namespace CatalogAdminService
