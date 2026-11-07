#pragma once

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"

#include <QDate>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

static int orderYearFromQueryValue(const QVariant& value)
{
    if (value.canConvert<int>()) {
        const int y = value.toInt();
        if (y > 1900 && y < 3000) return y;
    }
    return orderYearFromStoredDateTime(value.toString());
}

static QString createOrderUid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

static QString activeOrderUidForInternalNumber(QSqlDatabase database, int orderNumber)
{
    if (!database.isValid() || orderNumber <= 0) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    return (q.exec() && q.next()) ? q.value(0).toString().trimmed() : QString();
}

static int activeInternalNumberForOrderUid(QSqlDatabase database, const QString& orderUid)
{
    const QString uid = orderUid.trimmed();
    if (!database.isValid() || uid.isEmpty()) return 0;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT number FROM work_orders WHERE order_uid=? LIMIT 1"));
    q.addBindValue(uid);
    return (q.exec() && q.next()) ? q.value(0).toInt() : 0;
}

static QString routerCacheOrderUidForInternalNumber(QSqlDatabase database, int orderNumber)
{
    if (!database.isValid() || orderNumber <= 0) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM router_cache_work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    return (q.exec() && q.next()) ? q.value(0).toString().trimmed() : QString();
}

static int routerCacheInternalNumberForOrderUid(QSqlDatabase database, const QString& orderUid)
{
    const QString uid = orderUid.trimmed();
    if (!database.isValid() || uid.isEmpty()) return 0;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT number FROM router_cache_work_orders WHERE order_uid=? LIMIT 1"));
    q.addBindValue(uid);
    return (q.exec() && q.next()) ? q.value(0).toInt() : 0;
}

static bool upsertWorkspaceOrderState(QSqlDatabase database, int orderNumber, const QString& origin, const QString& sourceDb = QStringLiteral(""), const QString& orderUid = QString())
{
    return upsertWorkspaceOrderStateEntry(
        database,
        orderNumber,
        origin,
        sourceDb,
        orderUid,
        [&](int fallbackOrderNumber) {
            const QString activeUid = activeOrderUidForInternalNumber(database, fallbackOrderNumber).trimmed();
            return !activeUid.isEmpty() ? activeUid : routerCacheOrderUidForInternalNumber(database, fallbackOrderNumber).trimmed();
        });
}

} // namespace
