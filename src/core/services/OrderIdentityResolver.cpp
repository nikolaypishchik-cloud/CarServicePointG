#include "core/services/OrderIdentityResolver.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {

bool ensureOpen(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database connection");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }
    return true;
}

bool resolveByNumberImpl(QSqlDatabase database,
                         int orderNumber,
                         OrderIdentityResolver::OrderIdentityRecord* outRecord,
                         QString* errorText)
{
    if (orderNumber <= 0) {
        if (errorText) *errorText = QStringLiteral("Invalid order number");
        return false;
    }
    if (!ensureOpen(database, errorText)) return false;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT number, COALESCE(order_uid,''), COALESCE(NULLIF(order_number,0), number), COALESCE(order_year,0) "
        "FROM work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) {
        if (errorText) *errorText = QStringLiteral("Order not found: %1").arg(orderNumber);
        return false;
    }

    if (outRecord) {
        outRecord->number = q.value(0).toInt();
        outRecord->orderUid = q.value(1).toString().trimmed();
        outRecord->visibleOrderNumber = q.value(2).toInt();
        outRecord->orderYear = q.value(3).toInt();
    }
    return true;
}

} // namespace

namespace OrderIdentityResolver {

bool resolveByInternalNumber(QSqlDatabase database,
                             int orderNumber,
                             OrderIdentityRecord* outRecord,
                             QString* errorText)
{
    return resolveByNumberImpl(database, orderNumber, outRecord, errorText);
}

bool resolveByAnyUid(QSqlDatabase database,
                     const QString& anyOrderUid,
                     OrderIdentityRecord* outRecord,
                     QString* errorText)
{
    const QString uid = anyOrderUid.trimmed();
    if (uid.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Empty order uid");
        return false;
    }
    if (!ensureOpen(database, errorText)) return false;

    QSqlQuery direct(database);
    direct.prepare(QStringLiteral(
        "SELECT number, COALESCE(order_uid,''), COALESCE(NULLIF(order_number,0), number), COALESCE(order_year,0) "
        "FROM work_orders WHERE order_uid=? LIMIT 1"));
    direct.addBindValue(uid);
    if (direct.exec() && direct.next()) {
        if (outRecord) {
            outRecord->number = direct.value(0).toInt();
            outRecord->orderUid = direct.value(1).toString().trimmed();
            outRecord->visibleOrderNumber = direct.value(2).toInt();
            outRecord->orderYear = direct.value(3).toInt();
        }
        return true;
    }

    int orderNumber = 0;
    QSqlQuery router(database);
    router.prepare(QStringLiteral("SELECT COALESCE(number,0) FROM router_cache_work_orders WHERE order_uid=? LIMIT 1"));
    router.addBindValue(uid);
    if (router.exec() && router.next()) {
        orderNumber = router.value(0).toInt();
    }
    if (orderNumber <= 0) {
        QSqlQuery registry(database);
        registry.prepare(QStringLiteral("SELECT COALESCE(order_number,0) FROM order_registry WHERE order_uid=? LIMIT 1"));
        registry.addBindValue(uid);
        if (registry.exec() && registry.next()) {
            orderNumber = registry.value(0).toInt();
        }
    }
    if (orderNumber <= 0) {
        if (errorText) *errorText = QStringLiteral("Canonical order uid not found for %1").arg(uid);
        return false;
    }
    return resolveByNumberImpl(database, orderNumber, outRecord, errorText);
}

QString canonicalOrderUidForInternalNumber(QSqlDatabase database,
                                           int orderNumber,
                                           QString* errorText)
{
    OrderIdentityRecord record;
    if (!resolveByInternalNumber(database, orderNumber, &record, errorText)) return QString();
    return record.orderUid;
}

QString canonicalOrderUidForAnyUid(QSqlDatabase database,
                                   const QString& anyOrderUid,
                                   QString* errorText)
{
    OrderIdentityRecord record;
    if (!resolveByAnyUid(database, anyOrderUid, &record, errorText)) return QString();
    return record.orderUid;
}

} // namespace OrderIdentityResolver
