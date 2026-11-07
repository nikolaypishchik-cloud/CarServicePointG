#include "core/services/OrderArchiveRotationService.h"

#include "core/services/OrderStoragePolicyService.h"
#include "infrastructure/db/sqlite/Db.h"

namespace OrderArchiveRotationService {

int activeClosedOrderLimit()
{
    return OrderStoragePolicyService::activeClosedOrderLimit();
}

bool enforcePolicy(QString* errorText)
{
    return Database::rotateActiveClosedOrders(errorText);
}

bool archiveSingleClosedOrder(int internalOrderNumber, QString* errorText)
{
    return Database::archiveSingleActiveOrderToArchive(internalOrderNumber, errorText);
}

bool cleanupHydratedArchiveCopies(QString* errorText)
{
    return Database::cleanupHydratedArchiveCopies(errorText);
}

} // namespace OrderArchiveRotationService
