#pragma once

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"
#include "core/services/OrderIdentityService.h"

#include <QDate>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

static bool createDraftOrderShellInActive(QSqlDatabase database, int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return false;
    if (!database.isValid() || (!database.isOpen() && !database.open())) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    QSqlQuery exists(database);
    exists.prepare(QStringLiteral("SELECT 1 FROM work_orders WHERE number=? LIMIT 1"));
    exists.addBindValue(orderNumber);
    if (exists.exec() && exists.next()) {
        return true;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const int orderYear = OrderIdentityService::orderYearForDate(now.date());

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO work_orders(number, order_uid, order_number, order_year, opened_at, closed_at, status, plate, plate_norm, vin, car_year, odometer, engine_type, engine_volume, fuel_type, car, client, client_phone, master_id, works_sum, parts_sum, discount, master_name_closed, car_model, car_brand, car_kind, color, car_color, owner_name) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(orderNumber);
    q.addBindValue(OrderIdentityService::createOrderUid());
    q.addBindValue(orderNumber);
    q.addBindValue(orderYear);
    q.addBindValue(now.toString(Qt::ISODate));
    q.addBindValue(QVariant(QVariant::String));
    q.addBindValue(QStringLiteral("open"));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(0);
    q.addBindValue(0);
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(0.0);
    q.addBindValue(0.0);
    q.addBindValue(0.0);
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(QStringLiteral(""));

    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

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
