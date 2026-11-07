#include "core/services/orders/WorkPartsReadQueryService.h"

#include "core/services/OrderIdentityResolver.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/services/orders/OutsourceJobService.h"
#include "infrastructure/db/sqlite/Db.h"

QList<WorkOrderServiceRow> WorkPartsReadQueryService::listServicesByInternalNumber(int orderNumber,
                                                                                    QString* errorText)
{
    return OrderDetailsRepository::listServicesByInternalNumber(Database::db(), orderNumber, errorText);
}

QList<WorkOrderPartRow> WorkPartsReadQueryService::listPartsByInternalNumber(int orderNumber,
                                                                              QString* errorText)
{
    return OrderDetailsRepository::listPartsByInternalNumber(Database::db(), orderNumber, errorText);
}

QList<WorkOrderServiceRow> WorkPartsReadQueryService::listServicesByUid(const QString& orderUid,
                                                                         QString* errorText)
{
    return OrderDetailsRepository::listServicesByUid(Database::db(), orderUid, errorText);
}

QList<WorkOrderPartRow> WorkPartsReadQueryService::listPartsByUid(const QString& orderUid,
                                                                   QString* errorText)
{
    return OrderDetailsRepository::listPartsByUid(Database::db(), orderUid, errorText);
}

QString WorkPartsReadQueryService::canonicalOrderUidForInternalNumber(int orderNumber)
{
    return OrderIdentityResolver::canonicalOrderUidForInternalNumber(Database::db(), orderNumber);
}

QHash<int, QString> WorkPartsReadQueryService::loadOutsourceServiceDisplayNames(const QString& orderUid)
{
    return OutsourceJobService::loadServiceDisplayNames(Database::db(), orderUid);
}

QHash<int, OrderAttachmentService::AttachmentInfo> WorkPartsReadQueryService::serviceAttachmentMap(const QList<int>& serviceIds)
{
    return OrderAttachmentService::serviceAttachmentMap(Database::db(), serviceIds);
}

QHash<int, OrderAttachmentService::AttachmentInfo> WorkPartsReadQueryService::partAttachmentMap(const QList<int>& partIds)
{
    return OrderAttachmentService::partAttachmentMap(Database::db(), partIds);
}

QString WorkPartsReadQueryService::serviceAttachmentPath(int serviceId, QString* displayName)
{
    return OrderAttachmentService::serviceAttachmentPath(Database::db(), serviceId, displayName);
}

QString WorkPartsReadQueryService::partAttachmentPath(int partId,
                                                      QString* displayName,
                                                      QString* invoiceNumber,
                                                      QString* invoiceDate)
{
    return OrderAttachmentService::partAttachmentPath(Database::db(), partId, displayName, invoiceNumber, invoiceDate);
}
