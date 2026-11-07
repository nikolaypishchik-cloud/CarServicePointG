#pragma once

#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"

#include <QSqlDatabase>
#include <QSqlQuery>

namespace {

static QString activeOrderUidForInternalNumber_local(QSqlDatabase database, int orderNumber)
{
    if (!database.isValid() || orderNumber <= 0) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT NULLIF(TRIM(COALESCE(order_uid,'')), '') FROM work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    if (!q.exec() || !q.next()) return QString();
    return q.value(0).toString().trimmed();
}

static QString routerCacheOrderUidForInternalNumber_local(QSqlDatabase database, int orderNumber)
{
    if (!database.isValid() || orderNumber <= 0) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT NULLIF(TRIM(COALESCE(order_uid,'')), '') FROM router_cache_work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    if (!q.exec() || !q.next()) return QString();
    return q.value(0).toString().trimmed();
}

static bool upsertWorkspaceOrderState_local(QSqlDatabase database, int orderNumber, const QString& origin, const QString& sourceDb = QStringLiteral(""), const QString& orderUid = QString())
{
    return upsertWorkspaceOrderStateEntry(
        database,
        orderNumber,
        origin,
        sourceDb,
        orderUid,
        [&](int fallbackOrderNumber) {
            const QString activeUid = activeOrderUidForInternalNumber_local(database, fallbackOrderNumber).trimmed();
            return !activeUid.isEmpty() ? activeUid : routerCacheOrderUidForInternalNumber_local(database, fallbackOrderNumber).trimmed();
        });
}

} // namespace
