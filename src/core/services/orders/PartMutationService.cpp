#include "core/services/orders/PartMutationService.h"

#include "core/services/OrderIdentityResolver.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"
#include "core/repository/RepositoryWriteBoundary.h"

#include <QDebug>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
QString safeText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}
bool readRuntimePartOwner(QSqlDatabase db,int partId,int expectedNumber,int* number,QString* uid,QString* errorText)
{
    auto fail=[&](const QString& text){if(errorText)*errorText=text;return false;};
    *number=0;uid->clear();QSqlQuery read(db);
    if(!read.prepare(QStringLiteral("SELECT p.order_number,p.order_uid,w.order_uid FROM work_order_parts p LEFT JOIN work_orders w ON w.number=p.order_number WHERE p.id=?")))return fail(read.lastError().text());
    read.addBindValue(partId);if(!read.exec())return fail(read.lastError().text());
    if(!read.next()) {
        if(read.lastError().isValid())return fail(read.lastError().text());
        return expectedNumber<=0 || fail(QStringLiteral("Part was not found"));
    }
    const int actualNumber=read.value(0).toInt();const QString actualUid=read.value(1).toString();
    // A genuinely unassigned stock row has no order projection to update.
    if(actualNumber<=0 && actualUid.trimmed().isEmpty() && expectedNumber<=0)return true;
    if(actualNumber<=0 || actualUid.trimmed().isEmpty() || read.value(2).toString()!=actualUid || (expectedNumber>0 && actualNumber!=expectedNumber))
        return fail(QStringLiteral("Part UID/internal parent requires reconciliation"));
    *number=actualNumber;*uid=actualUid;return true;
}
}

namespace PartMutationService {

bool updateInstalledAt(QSqlDatabase db,
                       int orderNumber,
                       int partId,
                       const QString& installedAtIso,
                       QString* errorText)
{
    if (partId <= 0) return true;

    const QString orderUid = OrderIdentityResolver::canonicalOrderUidForInternalNumber(db, orderNumber).trimmed();
    if (orderUid.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Order uid not found for order %1").arg(orderNumber);
        return false;
    }

    // Installation checkbox changes only installed_at. Do not route this narrow
    // date-only mutation through PartRepository::update(), because that method
    // validates the full pricing invariant and can reject older imported rows
    // whose prices were saved before markup normalization. Marking a part as
    // installed must not rewrite or revalidate price/markup fields.
    RepositoryWriteBoundary boundary(db,errorText);if(!boundary.started())return false;
    const bool runtime=DbThreadConnectionSupport::isRuntimeConnection(db.connectionName());int nativeNumber=0;QString nativeUid;
    if(runtime && !readRuntimePartOwner(db,partId,orderNumber,&nativeNumber,&nativeUid,errorText))return false;
    if(runtime && nativeUid!=orderUid) {if(errorText)*errorText=QStringLiteral("Part UID/internal parent requires reconciliation");return false;}
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE work_order_parts SET installed_at=? WHERE id=? AND order_uid=?")+(runtime?QStringLiteral(" AND order_number=?"):QString()));
    q.addBindValue(safeText(installedAtIso));
    q.addBindValue(partId);
    q.addBindValue(orderUid);
    if(runtime)q.addBindValue(nativeNumber);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        qCritical().noquote() << QStringLiteral("DB_MUTATION part_installed_at_update_failed order=") << orderNumber
                              << QStringLiteral(" orderUid=") << orderUid
                              << QStringLiteral(" partId=") << partId
                              << QStringLiteral(" error=") << q.lastError().text();
        return false;
    }

    if (q.numRowsAffected() > 0) {
        q.finish();
        if(DbThreadConnectionSupport::isRuntimeConnection(db.connectionName()) && !queueOrderRegistryPublication(db,orderNumber,errorText)) return false;
        return boundary.finish();
    }

    // SQLite can report zero affected rows for a no-op update. Verify ownership
    // before treating it as an error.
    QSqlQuery verify(db);
    verify.prepare(QStringLiteral("SELECT COALESCE(order_uid,''), COALESCE(order_number,0) FROM work_order_parts WHERE id=? LIMIT 1"));
    verify.addBindValue(partId);
    if (!verify.exec()) {
        if (errorText) *errorText = verify.lastError().text();
        return false;
    }
    if (!verify.next()) {
        if (errorText) *errorText = QStringLiteral("Рядок запчастини не знайдено");
        return false;
    }

    const QString actualOrderUid = verify.value(0).toString().trimmed();
    const int actualOrderNumber = verify.value(1).toInt();
    if (actualOrderUid != orderUid) {
        if (errorText) {
            *errorText = QStringLiteral("Part installed_at UPDATE matched no row: partId=%1 expectedOrder=%2 expectedOrderUid=%3 actualOrder=%4 actualOrderUid=%5")
                .arg(partId)
                .arg(orderNumber)
                .arg(orderUid)
                .arg(actualOrderNumber)
                .arg(actualOrderUid);
        }
        qCritical().noquote() << QStringLiteral("DB_MUTATION part_installed_at_update_order_uid_mismatch order=") << orderNumber
                              << QStringLiteral(" orderUid=") << orderUid
                              << QStringLiteral(" partId=") << partId
                              << QStringLiteral(" actualOrder=") << actualOrderNumber
                              << QStringLiteral(" actualOrderUid=") << actualOrderUid;
        return false;
    }

    qWarning().noquote() << QStringLiteral("DB_MUTATION part_installed_at_update_noop_verified order=") << orderNumber
                         << QStringLiteral(" orderUid=") << orderUid
                         << QStringLiteral(" partId=") << partId;
    return boundary.finish();
}

bool ensureDeliveryDateIfMissing(QSqlDatabase db,
                                 int partId,
                                 const QString& deliveryDateIso,
                                 QString* errorText)
{
    if (partId <= 0) return true;
    RepositoryWriteBoundary boundary(db,errorText);if(!boundary.started())return false;
    const bool runtime=DbThreadConnectionSupport::isRuntimeConnection(db.connectionName());int nativeNumber=0;QString nativeUid;
    if(runtime && !readRuntimePartOwner(db,partId,0,&nativeNumber,&nativeUid,errorText))return false;
    QSqlQuery q(db);
    const QString ownerPredicate=!runtime?QString():nativeNumber>0?QStringLiteral(" AND order_number=? AND order_uid=?"):QStringLiteral(" AND COALESCE(order_number,0)<=0 AND TRIM(COALESCE(order_uid,''))=''");
    q.prepare(QStringLiteral("UPDATE work_order_parts SET delivery_date=? WHERE id=? AND TRIM(COALESCE(delivery_date,''))=''")+ownerPredicate);
    q.addBindValue(safeText(deliveryDateIso));
    q.addBindValue(partId);
    if(runtime && nativeNumber>0){q.addBindValue(nativeNumber);q.addBindValue(nativeUid);}
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    const bool changed=q.numRowsAffected()>0;q.finish();
    if(changed && runtime && nativeNumber>0 && !queueOrderRegistryPublication(db,nativeNumber,errorText))return false;
    return boundary.finish();
}

bool updateInstalledAt(int orderNumber,
                       int partId,
                       const QString& installedAtIso,
                       QString* errorText)
{
    return updateInstalledAt(Database::db(), orderNumber, partId, installedAtIso, errorText);
}

bool ensureDeliveryDateIfMissing(int partId,
                                 const QString& deliveryDateIso,
                                 QString* errorText)
{
    return ensureDeliveryDateIfMissing(Database::db(), partId, deliveryDateIso, errorText);
}

} // namespace PartMutationService
