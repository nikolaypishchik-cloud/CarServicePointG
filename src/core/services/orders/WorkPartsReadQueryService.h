#pragma once

#include "core/services/OrderDetailsRepository.h"
#include "core/services/orders/OrderAttachmentService.h"

#include <QHash>
#include <QString>

class WorkPartsReadQueryService
{
public:
    static QList<WorkOrderServiceRow> listServicesByInternalNumber(int orderNumber,
                                                                   QString* errorText = nullptr);
    static QList<WorkOrderPartRow> listPartsByInternalNumber(int orderNumber,
                                                             QString* errorText = nullptr);
    static QList<WorkOrderServiceRow> listServicesByUid(const QString& orderUid,
                                                        QString* errorText = nullptr);
    static QList<WorkOrderPartRow> listPartsByUid(const QString& orderUid,
                                                  QString* errorText = nullptr);

    static QString canonicalOrderUidForInternalNumber(int orderNumber);
    static QHash<int, QString> loadOutsourceServiceDisplayNames(const QString& orderUid);
    static QHash<int, OrderAttachmentService::AttachmentInfo> serviceAttachmentMap(const QList<int>& serviceIds);
    static QHash<int, OrderAttachmentService::AttachmentInfo> partAttachmentMap(const QList<int>& partIds);

    static QString serviceAttachmentPath(int serviceId, QString* displayName = nullptr);
    static QString partAttachmentPath(int partId,
                                      QString* displayName = nullptr,
                                      QString* invoiceNumber = nullptr,
                                      QString* invoiceDate = nullptr);
};
