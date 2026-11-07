#include "core/services/AppRuntimeService.h"

#include "infrastructure/db/sqlite/Db.h"

namespace AppRuntimeService {

bool ensureConnectionAlive(QString* errorText)
{
    return Database::ensureConnectionAlive(errorText);
}

void closeThreadConnection()
{
    Database::close();
}

QString resolveOrderUid(int orderNumber)
{
    return Database::resolveOrderUid(orderNumber);
}

bool hardDeleteOrder(int orderNumber, QString* errorText)
{
    return Database::hardDeleteOrder(orderNumber, errorText);
}

bool loadSyncAutoPartsEnabled()
{
    return Database::getSetting(QStringLiteral("settings.int.sync.auto_parts"), true).toBool();
}

bool setPartPaid(int partId, bool paid)
{
    return Database::setPartPaid(partId, paid);
}

void refreshOrderRegistry(int orderNumber)
{
    Database::refreshOrderRegistry(orderNumber, nullptr);
}

} // namespace AppRuntimeService

QString AppRuntimeService::activeDatabasePath() { return Database::db().databaseName(); }
