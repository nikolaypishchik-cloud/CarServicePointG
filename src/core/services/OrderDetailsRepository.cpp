#include "core/services/OrderDetailsRepository.h"

#include "core/services/OrderIdentityResolver.h"

namespace OrderDetailsRepository {

QList<WorkOrderServiceRow> listServicesByInternalNumber(QSqlDatabase database,
                                                        int orderNumber,
                                                        QString* errorText)
{
    const QString orderUid = OrderIdentityResolver::canonicalOrderUidForInternalNumber(database, orderNumber, errorText);
    if (orderUid.trimmed().isEmpty()) return {};
    return ServiceRepository::listByOrderUid(database, orderUid);
}

QList<WorkOrderPartRow> listPartsByInternalNumber(QSqlDatabase database,
                                                  int orderNumber,
                                                  QString* errorText)
{
    const QString orderUid = OrderIdentityResolver::canonicalOrderUidForInternalNumber(database, orderNumber, errorText);
    if (orderUid.trimmed().isEmpty()) return {};
    return PartRepository::listByOrderUid(database, orderUid);
}

QList<WorkOrderServiceRow> listServicesByUid(QSqlDatabase database,
                                             const QString& orderUid,
                                             QString* errorText)
{
    const QString canonicalUid = OrderIdentityResolver::canonicalOrderUidForAnyUid(database, orderUid, errorText);
    if (canonicalUid.trimmed().isEmpty()) return {};
    return ServiceRepository::listByOrderUid(database, canonicalUid);
}

QList<WorkOrderPartRow> listPartsByUid(QSqlDatabase database,
                                       const QString& orderUid,
                                       QString* errorText)
{
    const QString canonicalUid = OrderIdentityResolver::canonicalOrderUidForAnyUid(database, orderUid, errorText);
    if (canonicalUid.trimmed().isEmpty()) return {};
    return PartRepository::listByOrderUid(database, canonicalUid);
}

} // namespace OrderDetailsRepository
