// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsPersistenceCoordinator.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

void WorkPartsDialog::autoAssignPartsToServices()
{
    struct ServiceInfo { int id = 0; QString title; };
    struct PartInfo { int id = 0; QString name; int serviceId = 0; };

    const QString orderUid = currentOrderUid();
    if (orderUid.isEmpty()) return;
    QList<ServiceInfo> services;
    const auto serviceRows = WorkPartsReadQueryService::listServicesByUid(orderUid);
    for (const auto& row : serviceRows) {
        ServiceInfo s;
        s.id = row.id;
        s.title = row.title;
        services.append(s);
    }
    if (services.isEmpty()) return;

    QList<PartInfo> parts;
    const auto partRows = WorkPartsReadQueryService::listPartsByUid(orderUid);
    for (const auto& row : partRows) {
        PartInfo p;
        p.id = row.id;
        p.name = row.name;
        p.serviceId = row.serviceId;
        parts.append(p);
    }

    QList<PartServiceAssignmentService::ServiceRow> assignmentServices;
    assignmentServices.reserve(services.size());
    for (const auto& service : services) {
        PartServiceAssignmentService::ServiceRow row;
        row.id = service.id;
        row.title = service.title;
        assignmentServices.push_back(row);
    }

    QList<PartServiceAssignmentService::PartRow> assignmentParts;
    assignmentParts.reserve(parts.size());
    for (const auto& part : parts) {
        PartServiceAssignmentService::PartRow row;
        row.id = part.id;
        row.name = part.name;
        row.serviceId = part.serviceId;
        assignmentParts.push_back(row);
    }

    QString assignError;
    const int changedAssignments = PartServiceAssignmentService::autoAssign(
        !orderUid.isEmpty() ? orderUid : QString::number(m_orderNumber),
        assignmentServices,
        assignmentParts,
        &assignError);

    if (!assignError.trimmed().isEmpty()) {
        qWarning() << "PartServiceAssignmentService::autoAssign failed:" << assignError;
    }

    if (changedAssignments > 0) {
        publishWorkPartsBackupEvent(QStringLiteral("order.parts"),
                                    QStringLiteral("part_assignment"),
                                    QString::number(changedAssignments),
                                    orderUid,
                                    m_orderNumber,
                                    QStringLiteral("auto_assign_service"),
                                    QString::fromUtf8("Автоприв'язано запчастини до робіт"),
                                    {QStringLiteral("work_order_parts")});
    }
}

